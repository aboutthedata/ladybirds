// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <assert.h>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "graph/graph.h"
#include "graph/itemmap.h"
#include "lua/pass.h"
#include "msgui.h"
#include "program.h"
#include "spacedivision.h"
#include "taskgroup.h"
#include "tools.h"


using Ladybirds::gen::Space;
using Ladybirds::gen::SpaceDivision;
using Ladybirds::impl::Port;
using Ladybirds::impl::Program;
using Ladybirds::spec::Dependency;
using Ladybirds::impl::TaskGroup;
using Ladybirds::spec::TaskGraph;
using Ladybirds::spec::Task;
using Ladybirds::spec::Iface;
using Ladybirds::lua::Pass;
using Ladybirds::graph::PresDeque;
using Ladybirds::graph::ItemMap;

namespace {

constexpr bool gDbgOut = false;

struct TransientArgs : public Ladybirds::loadstore::LoadStorableCompound
{
    double ReadCost = 0, WriteCost = 0, StartupCost = 0, MaxBurstCost = 0;
    bool Greedy = false;
    
    virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override
    { return ls.IO("readcost", ReadCost)
           & ls.IO("writecost", WriteCost, false)
           & ls.IO("startupcost", StartupCost)
           & ls.IO("greedy", Greedy, false, false)
           & ls.IO("maxburstcost", MaxBurstCost, false, 0, 0);
    }
};
struct TransientRets : public Ladybirds::loadstore::LoadStorableCompound
{
    double MaxBurstCost = -1;
    double TotalCost = -1;
    
    virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override
    { return ls.IO("maxburstcost", MaxBurstCost, false)
           & ls.IO("totalcost", TotalCost, false);
    }    
};
bool GroupForTransient(Program &prog, TransientArgs &args, TransientRets &rets);

/** Pass GroupForTransient: Groups the tasks such for transient single core computing.
 The goal is to minimize the overall energy needed to accomplish the task, but also to minimize the bust energy,
 i.e. the energy that must be stored by a capacitor such that each group can run without interruption. **/
Ladybirds::lua::PassWithArgsAndRet<TransientArgs, TransientRets> 
    GroupForTransientPass("GroupForTransient", &GroupForTransient,
        Pass::Requires{}, Pass::Destroys{"CalcSuccessorMatrix", "LoadMapping", "PopulateGroups"});

using OutDepMap = std::unordered_map<const Iface*, std::deque<const Dependency*>>;

OutDepMap GetOutDeps(const Program::DepList &deps)
{
    OutDepMap ret;
    for(auto &dep : deps) ret[dep.From.TheIface].push_back(&dep);
    return ret;
}


/// \internal The actual working horse of EstablishExecutionOrder. Uses a greedy heuristic implemented recursively.
void ScheduleNodes(const Task &t, const std::vector<const Task*> &alltasks, const ItemMap<ItemMap<int>> &weights,
                   ItemMap<int> &depctr, std::vector<const Task*> &order)
{
    for(auto & e : t.OutEdges()) --depctr[e.GetTarget()];
    depctr[t] = -1;
    
    auto &tweights = weights[t];
    auto taskcmp = [&tweights](auto pt1, auto pt2){ return tweights[pt1] < tweights[pt2]; };
    std::priority_queue<const Task*, std::vector<const Task*>, decltype(taskcmp)> candidates(taskcmp, alltasks);
    
    while(!candidates.empty())
    {
        auto pcand = candidates.top();
        candidates.pop();
        
        if(tweights[pcand] < 0) break;
        if(depctr[pcand] != 0) continue;
        
        order.push_back(pcand);
        ScheduleNodes(*pcand, alltasks, weights, depctr, order);
    }
}


/** \internal Determines a single core execution order which is supposed to allow a small burst energy.
 *  Then reorders the nodes in the task graph accordingly.
 **/
void EstablishExecutionOrder(Program &prog, const OutDepMap &outdeps)
{
    auto &tg = prog.TaskGraph;
    
    auto * rootnode = tg.EmplaceNode();
    
    auto weights = tg.GetNodeMap(tg.GetNodeMap(int(-1)));
    auto addweigth = [](int &weight, int add) { if (weight < 0) weight = add; else weight += add; };
    
    for(auto &dep : prog.Dependencies)
    {
        auto &fromiface = *dep.From.TheIface, &toiface = *dep.To.TheIface;
        auto &type = fromiface.GetPacket()->GetBaseType();
        addweigth(weights[fromiface.GetTask()][toiface.GetTask()], dep.From.Index.GetVolume() * type.Size);
    }
    
    for(auto &entry : outdeps)
    {
        auto basesize = entry.first->GetPacket()->GetBaseType().Size;
        auto &deps = entry.second;
        for(auto it = deps.begin(), itend = deps.end(); it != itend; ++it)
        {
            auto task1 = (*it)->To.TheIface->GetTask();
            auto &targetweigths = weights[task1];
            
            for(auto it2 = deps.begin(); it2 != it; ++it2)
            {
                auto task2 = (*it2)->To.TheIface->GetTask();
                if(task1 == task2) continue;
                
                auto weight = ((*it)->From.Index & (*it2)->From.Index).GetVolume() * basesize;
                addweigth(targetweigths[task2], weight);
                addweigth(weights[task2][task1], weight);
            }
        }
    }

    auto depctr = tg.GetNodeMap(0);
    for(auto &n : tg.Nodes())
    {
        auto nin = n.InEdgeCount();
        if(nin == 0 && &n != rootnode)
        {
            tg.EmplaceEdge(rootnode, &n);
            weights[rootnode][n] = 0;
            nin = 1;
        }
        depctr[n] = nin;
    }
    
    std::vector<const Task*> alltasks, order;
    order.reserve(tg.Nodes().size());
    
    alltasks.reserve(tg.Nodes().size());
    for(auto &t : tg.Nodes()) alltasks.push_back(&t);
    
    ScheduleNodes(*rootnode, alltasks, weights, depctr, order);
    
    if(gDbgOut)
    {
        auto &strm = gMsgUI.Info("New order:");
        for(auto *pt : order) strm << pt->Name << " ";
        strm << std::endl;
    }
    
    tg.RemoveNode(rootnode);
    tg.ReorderNodes(order);
}

/// \internal stores all necessary information about the edges and how they overlap when being combined in bursts
struct DepCost
{
    using ReadCostMap = std::map<TaskGraph::ID_t, double>;
    ReadCostMap ReadCost;
    double WriteCost;
    TaskGraph::ID_t FromID;
};

/// \internal calculates the DepCosts for all edges
auto CalcDepCosts(OutDepMap &outdeps, double readcost, double writecost)
{
    std::unordered_map<const Dependency*, DepCost> ret;
    
    for(auto &entry : outdeps)
    {
        auto basesize = entry.first->GetPacket()->GetBaseType().Size;
        double itemreadcost = readcost*basesize, itemwritecost=writecost*basesize;
        auto fromid = entry.first->GetTask()->GetID();
        auto gettoid = [](const auto *pdep){return pdep->To.TheIface->GetTask()->GetID(); };
        
        auto &deps = entry.second;
        Sort(deps, gettoid);
        
        SpaceDivision<const Dependency*> sd((Space(entry.first->GetDimensions()))), sd2 = sd;
        for(auto *pdep : deps) sd.AssignSection(pdep->From.Index, pdep);
        
        for(auto it = deps.begin(), itend = deps.end(); it < itend; ++it)
        {
            auto pdep = *it;
            DepCost::ReadCostMap rcmap;
            sd2.Clear();
            for(auto it2 = it, it2end = std::prev(deps.begin()); it2 != it2end; )
            {
                auto pdep2 = *it2;
                sd2.AssignSection(pdep2->From.Index, pdep2);
                
                auto toid = gettoid(pdep2);
                if(--it2 != it2end && gettoid(*it2) == toid) continue;
                
                auto cost = sd2.GetEnvelope(pdep).GetVolume()*itemreadcost;
                rcmap.emplace_hint(rcmap.begin(), toid, cost);
                if(cost == 0) break;
            }
            
            ret.emplace(pdep, DepCost({
                /*ReadCost*/  std::move(rcmap),
                /*WriteCost*/ sd.GetEnvelope(pdep).GetVolume()*itemwritecost,
                /*FromID*/ fromid
            }));
        }
    }
    return ret;
}

using DistanceTable = ItemMap<ItemMap<double>>;
/** \internal Calculates a distance table for the burst partitioning algorithm.
 *  The return value is a NodeMap of NodeMaps. CalcDistanceTable(...)[task1][task2] means energy for a burst that 
 *  starts on task1 and ends on task2.
 **/
DistanceTable CalcDistanceTable(const TaskGraph &tg, const Program::DepList &deps, OutDepMap &ifaceoutdeps,
                       double readcost, double writecost, double startupcost)
{
    auto depcosts = CalcDepCosts(ifaceoutdeps, readcost, writecost);
    auto outdeps = tg.GetNodeMap<std::deque<const DepCost*>>(), indeps = outdeps;
    for(auto &dep : deps)
    {
        auto *pcosts = &depcosts[&dep];
        outdeps[dep.From.TheIface->GetTask()].push_back(pcosts);
        indeps[dep.To.TheIface->GetTask()].push_back(pcosts);
    }
    
    DistanceTable ret = tg.GetNodeMap(tg.GetNodeMap(std::numeric_limits<double>::infinity()));
    for(auto it1 = tg.NodesBegin(), itend = tg.NodesEnd(); it1 != itend; ++it1)
    {
        double cost = startupcost;
        auto minid = it1->GetID();
        for(auto it2 = it1; it2 != itend; ++it2)
        {
            auto &t = *it2;
            cost += t.Cost;
            
            for(auto *pdc : outdeps[t])
                cost += pdc->WriteCost;
            for(auto *pdc : indeps[t])
            {
                if(pdc->FromID >= minid)
                { //Creation and consumption in same burst: No read cost. Moreover, remove write cost we added before
                    cost -= pdc->WriteCost;
                }
                else
                {
                    auto itrc = pdc->ReadCost.lower_bound(minid);
                    assert(itrc != pdc->ReadCost.end());
                    cost += itrc->second;
                }
            }
            
            ret[*it1][*it2] = cost;
        }
    }
    
    return ret;
}

void PrintDistanceTable(const TaskGraph &tg, const DistanceTable &dist)
{
    for(auto &n1 : tg.Nodes())
    {
        auto &dist_n1 = dist[n1];
        printf("     inf ");
        for(auto &n2 : tg.Nodes())
        {
            printf("%8.1f ", dist_n1[n2]);
        }
        printf("\n");
    }
    for(int i = tg.Nodes().size();  i>= 0; i--)
    {
        printf("     inf ");
    }
    printf("\n\n");
}

/// \internal Modified Dijkstra algorithm for finding the shortest path when the nodes are already ordered
template<typename combinefunc>
double FindShortestPath(const TaskGraph &tg, const DistanceTable &dist, combinefunc combine,
                        std::vector<const Task*> *ppath = nullptr)
{
    auto shortest = tg.GetNodeMap(std::numeric_limits<double>::infinity());
    auto pred = tg.GetNodeMap<const Task*>(nullptr);
    
    double shortest_n1 = 0;
    const Task *pred_n1 = nullptr;
    for(auto it1 = tg.NodesBegin(), itend = tg.NodesEnd(); it1 != itend; ++it1)
    {
        auto &n1 = *it1;
        auto &n1_dists = dist[n1];
        for(auto it2 = it1; it2 != itend; ++it2)
        {
            auto &n2 = *it2;
            auto altcost = combine(shortest_n1, n1_dists[n2]);
            if(altcost < shortest[n2])
            {
                shortest[n2] = altcost;
                pred[n2] = pred_n1;
            }
        }
        
        shortest_n1 = shortest[n1];
        pred_n1 = &n1;
    }
    
    auto *plasttask = &*--tg.NodesEnd();
    if(ppath)
    {
        int len = 0;
        for(auto *p = plasttask; p != nullptr; p = pred[p]) ++len;
        ppath->assign(len, nullptr);
        for(auto *p = plasttask; p != nullptr; p = pred[p]) (*ppath)[--len] = p;
    }
    
    return shortest[plasttask];
}

double GetRealMaxBurstCost(const TaskGraph &tg,  const DistanceTable &dist, const std::vector<const Task*> &groupends)
{
    double maxburstcost = 0;
    const Task *pgroupstart = nullptr;
    auto ppgroupend = groupends.data();

    for(auto &n : tg.Nodes())
    {
        if(!pgroupstart) pgroupstart = &n;
        if(&n == *ppgroupend)
        {
            auto curcost = dist[pgroupstart][*ppgroupend];
            if(curcost > maxburstcost) maxburstcost = curcost;
            
            pgroupstart = nullptr;
            ++ppgroupend;
        }
    }
    return maxburstcost;
}

/// \internal Optimally partitions the program into bursts (groups) using a shortest path algorithm
void FindOptimalBursts(TaskGraph &tg, const Program::DepList &deps, OutDepMap &outdeps,
                       double readcost, double writecost, double startupcost, double maxburstcost,
                       std::vector<const Task*> &groupends, TransientRets &rets)
{
    auto dist = CalcDistanceTable(tg, deps, outdeps, readcost, writecost, startupcost);
    if(gDbgOut)
    {
        gMsgUI.Info("Distance table:");
        PrintDistanceTable(tg, dist);
    }
    
    bool calcmincap = (maxburstcost <= 0);
    if(calcmincap)
    {
        maxburstcost = FindShortestPath(tg, dist, [](auto d1, auto d2) {return std::max(d1, d2);});
        if(gDbgOut) gMsgUI.Info("Burst calculation: Estimated minimum capacity as %f", maxburstcost);
    }
    
    for(auto &n1 : tg.Nodes())
    {
        auto &n1_dists = dist[n1];
        for(auto &n2 : tg.Nodes())
        {
            if(n1_dists[n2] > maxburstcost)
                n1_dists[n2] = std::numeric_limits<double>::infinity();
        }
    }
    
    auto minenergy = FindShortestPath(tg, dist, std::plus<double>(), &groupends);
    if(gDbgOut) gMsgUI.Info("Burst calculation: Estimated total energy as %f", minenergy);
    rets.TotalCost = minenergy;
    
    if(!calcmincap) maxburstcost = GetRealMaxBurstCost(tg, dist, groupends);
    rets.MaxBurstCost = maxburstcost;
}

/// \internal Partitions the program into bursts (groups) following a greedy algorithm
void FindGreedyBursts(TaskGraph &tg, const Program::DepList &deps, OutDepMap &outdeps,
                       double readcost, double writecost, double startupcost, double maxburstcost,
                       std::vector<const Task*> &groupends, TransientRets &rets)
{
    auto dist = CalcDistanceTable(tg, deps, outdeps, readcost, writecost, startupcost);
    if(gDbgOut)
    {
        gMsgUI.Info("Distance table:");
        PrintDistanceTable(tg, dist);
    }
    
    bool calcmincap = (maxburstcost <= 0);
    if(calcmincap)
    {
        auto itmincap = std::max_element(tg.NodesBegin(), tg.NodesEnd(),
                                         [&dist](auto &n1, auto &n2) { return dist[n1][n1] < dist[n2][n2]; });
        maxburstcost = dist[*itmincap][*itmincap];
        if(gDbgOut) gMsgUI.Info("Greedy burst calculation: Estimated minimum capacity as %f", maxburstcost);
    }
    
    double totalcost = 0;
    for(auto it = tg.NodesBegin(), itend = tg.NodesEnd(); it != itend; )
    {
        auto &n1_dists = dist[*it];
        double curcost = n1_dists[*it];
        while(++it != itend)
        {
            double d = n1_dists[*it];
            if(d > maxburstcost) break;
            else curcost = d;
        }
        groupends.push_back(&*std::prev(it));
        totalcost += curcost;
    }
    
    if(gDbgOut) gMsgUI.Info("Burst calculation: Estimated total energy as %f", totalcost);
    rets.TotalCost = totalcost;
    
    if(!calcmincap) maxburstcost = GetRealMaxBurstCost(tg, dist, groupends);
    rets.MaxBurstCost = maxburstcost;
}

/** \internal Combines all the above functions to implement a pass that finds a scheduling and optimal burst
 *  partitioning for transient single-core systems.
 **/
bool GroupForTransient(Program &prog, TransientArgs &args, TransientRets &rets)
{
    if(!prog.Groups.empty())
    {
        gMsgUI.Error("GroupForTransient pass does not allow pre-existing groups");
        return false;
    }

    auto outdeps = GetOutDeps(prog.Dependencies);
    EstablishExecutionOrder(prog, outdeps);
    prog.TaskGraph.ClearEdges();
    std::adjacent_find(prog.TaskGraph.NodesBegin(), prog.TaskGraph.NodesEnd(),
                       [&prog](auto &t1, auto &t2){ prog.TaskGraph.EmplaceEdge(&t1, &t2); return false; });
    
    std::vector<const Task*> groupends;
    if(args.Greedy)
    {
        FindGreedyBursts(prog.TaskGraph, prog.Dependencies, outdeps, args.ReadCost, args.WriteCost,
                         args.StartupCost, args.MaxBurstCost, groupends, rets);
    }
    else
    {
        FindOptimalBursts(prog.TaskGraph, prog.Dependencies, outdeps, args.ReadCost, args.WriteCost,
                          args.StartupCost, args.MaxBurstCost, groupends, rets);
    }
    
    prog.Groups.reserve(groupends.size());
    const Task *const *groupend = groupends.data();

    auto upgroup = std::make_unique<TaskGroup>();
    for(auto &t : prog.TaskGraph.Nodes())
    {
        upgroup->AddTask(&t);
        t.Group = upgroup.get();
        if(&t == *groupend)
        {
            ++groupend;
            prog.Groups.push_back(std::move(upgroup));
            upgroup = std::make_unique<TaskGroup>();
        }
    }
    return true;
}

} //namespace ::
