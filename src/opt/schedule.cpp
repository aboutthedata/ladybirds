// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "schedule.h"

#include <queue>
#include <unordered_map>
#include <vector>

#include "gen/spacemultidiv.h"
#include "graph/graph.h"
#include "spec/platform.h"
#include "dependency.h"
#include "msgui.h"
#include "program.h"
#include "task.h"
#include "taskgroup.h"
#include "tools.h"

namespace Ladybirds { namespace opt {

using impl::Program;
using spec::Platform;
using spec::Task;
using spec::Packet;


class Schedule::TransitionImpl : public spec::Dependency
{
public:
    Tasknode *pSubstNode = nullptr;
    int Mem = -1;

public:
    TransitionImpl(spec::Dependency &dep, int mem = -1) : spec::Dependency(dep), Mem(mem) {}
    TransitionImpl(Tasknode *ptn, spec::Dependency &dep, int mem) : pSubstNode(ptn), Mem(mem)
        { To = dep.To; From.TheIface = nullptr; From.Index = gen::Space(To.Index.GetDimensions()); }
    TransitionImpl(spec::Dependency &dep, Tasknode *ptn, int mem) : pSubstNode(ptn), Mem(mem)
        { From = dep.From; To.TheIface = nullptr; To.Index = gen::Space(From.Index.GetDimensions()); }
};


class Schedule::Tasknode : public graph::Node<Schedule::Taskgraph, Dependency>
{
public:
    struct DataUse
    {
        std::vector<TransitionImpl*> Uses;
        long Size;
        int RefCount;
    };
    
public:
    spec::Task *pSpec;
    gen::Space *pTransferDims;
    Time Duration = 0;
    Time Alap = 0;
    Time Start = 0;
    int MemDiff = 0; // Difference in amount of memory used ("alive") before and after the task
    int TotalMemUse = 0;
    int OpenDependencies;
    std::vector<std::vector<DataUse>> DataDist;
    
    std::vector<int> Processors; //all processors involved in executing the task
    
public:
    Tasknode() : pSpec(nullptr) {}
    Tasknode(Task &t) : pSpec(&t) {}
    
    void CalcMemStats();
};

class Schedule::Dependency : public graph::Edge<Tasknode>
{
public:
    spec::Dependency *pSpec = nullptr;
    long Size = 0;
    Tasknode::DataUse *FromDist = nullptr;
    bool FromInout = false, ToInout = false;
    
    int Mem = -1;
    Time Offset = 0;
    
public:
    Dependency(spec::Dependency *pd) : pSpec(pd)
    {
        if(pd)
        {
            Size = pd->GetMemSize();
            auto isinout = [](spec::Dependency::Anchor &anc)
                {return anc.TheIface && (anc.TheIface->GetPacket()->GetAccessType() == spec::Packet::inout);};
            FromInout = isinout(pd->From);
            ToInout = isinout(pd->To);
        }
    }
};

void Schedule::Tasknode::CalcMemStats()
{
    MemDiff = TotalMemUse = 0;
    
    if(pSpec)
    {   //We need to evaluate the real interfaces here since output interfaces might not be connected
        for(auto &d : pSpec->Ifaces) TotalMemUse += d.GetMemSize();
        for(auto &e : InEdges()) MemDiff -= e.Size;
        for(auto &e : OutEdges()) MemDiff += e.Size;
    }
    else
    {
        TotalMemUse = OutEdgesBegin()->Size*2; // One buffer on the input side, one on the output side. Diff = 0.
    }
}

class Schedule::Taskgraph : public graph::Graph<Tasknode>
{
public:
    void CalcAlap()
    {
        auto depcnt = GetNodeMap<int>();
        for(auto &n : Nodes())
        {
            depcnt[n] = n.OutEdgeCount();
            n.Alap = 0;
        }
        
        for(auto &n : Nodes()) if(n.Alap == 0) AlapRecurse(n, depcnt);
    }
    
private:
    void AlapRecurse(Schedule::Tasknode &n, graph::ItemMap<int> &depcnt)
    {
        n.Alap -= n.Duration;
        for(auto &e : n.InEdges())
        {
            auto *pnfrom = e.GetSource();
            auto alapcond = n.Alap - e.Offset;
            if(pnfrom->Alap > alapcond) pnfrom->Alap = alapcond;
            if(--depcnt[pnfrom] == 0) AlapRecurse(*pnfrom, depcnt);
        }
    };

};


struct Schedule::SchedulingItem
{
    Time ReadyTime;
    long Priority;
    Tasknode *pTasknode = nullptr;
    
    bool operator<(const SchedulingItem &other) const
    {
        if(ReadyTime != other.ReadyTime) return ReadyTime > other.ReadyTime;
        else return Priority < other.Priority;
    }
    
    SchedulingItem(Tasknode &tn, long priority) : ReadyTime(tn.Start), Priority(priority), pTasknode(&tn) {}
    SchedulingItem(Time readytime, const SchedulingItem &other) : SchedulingItem(other) { ReadyTime = readytime;} 
};




Schedule::Schedule(Program &prog, Platform &pf)
    : Program_(prog), Platform_(pf), DmaIndexBase_(pf.GetCores().size()), 
      CoreOccs_(DmaIndexBase_+pf.GetDmaControllers().size()),
      RuntimeOccEnds_(pf.GetMemories().size()),
      upGraph_(std::make_unique<Taskgraph>())
{
    MemOccs_.reserve(pf.GetMemories().size());
    for(auto &mem : pf.GetMemories()) MemOccs_.emplace_back(mem.Size*95/100);
    GroupOccs_.reserve(pf.GetGroups().size());
    for(auto &grp : pf.GetGroups())
    {
        auto memsize = Sum(grp.GetMemories(), [](auto *pm) { return pm->Size; });
        GroupOccs_.emplace_back(memsize*95/100);
    }
}

Schedule::~Schedule() {} //For destruction of incomplete type (in class declaration) of Taskgraph


void Schedule::BuildGraph(IfaceMapping *pdm, SpillMapping *psm)
{
    assert((!pdm) == (!psm));
    upGraph_->Clear();
    
    // Insert task nodes for tasks
    auto nodemap = InsertTaskNodes();
    CalcTaskDurations(pdm);
    
    // Insert task nodes for data transfers and edges between them. Also, fill Transitions
    CalcTransitions(nodemap, pdm, psm);
    
    // Take Transitions and build uses and edges between nodes
    CalcDataDist();
    
    // Insert remaining edges from transitions
    InsertTransitionEdges(nodemap);
    
    // Calculate memory statistics for all nodes
    for(auto &n : upGraph_->Nodes()) n.CalcMemStats();
}

graph::ItemMap<Schedule::Tasknode*> Schedule::InsertTaskNodes()
{
    auto &graph = *upGraph_.get();
    auto ret = Program_.TaskGraph.GetNodeMap<Tasknode*>();

    for(auto &t : Program_.GetTasks())
{
        auto *pn = graph.EmplaceNode(t);
        pn->Processors.assign({t.Group->GetBinding()->Index});
        ret[t] = pn;
    }
    return ret;
}
    
bool Schedule::CalcTaskDurations(IfaceMapping *pdm)
{
    auto &connmap = Platform_.GetConnMap();
    for(auto &n : upGraph_->Nodes())
    {
        if(!n.pSpec) continue;
        
        Time dura = n.pSpec->Cost;
        for(auto &d : n.pSpec->Ifaces)
        {
            int rcost, wcost;
            if(pdm)
            {
                auto *pmem = pdm->at(&d);
                auto &core = Platform_.GetCores()[n.Processors.front()];
                auto *hwconn = connmap[core.pNode][pmem->pNode];
                if(!hwconn)
                {
                    gMsgUI.Error("Cannot access memory '%s' from core '%s' although the mapping says so",
                                 pmem->Name.c_str(), core.Name.c_str());
                    return false;
                }
                rcost = hwconn->ReadCost, wcost = hwconn->WriteCost;
            }
            else
            {
                rcost = wcost = 1000;
            }
            dura += rcost*d.Reads + wcost*d.Writes;
        }
        n.Duration = dura;
    }
    return true;
}

// Inserts data transition edges and data transfer tasks if necessary
bool Schedule::CalcTransitions(const graph::ItemMap<Tasknode*> &nodemap, IfaceMapping *pdm, SpillMapping *psm)
{
    if(!pdm)
    {
        // Assumption: We can always share buffers (no copies needed). This will have to be revised/refined later
        Transitions.assign(Program_.Dependencies.begin(), Program_.Dependencies.end());
        return true;
    }    
    
    auto connect = [this](Platform::Memory *pfrommem, Platform::Memory *ptomem, long size)
    {
        assert(pfrommem && ptomem);
        auto *pconn = Platform_.GetConnMap()[pfrommem->pNode][ptomem->pNode];
        if(!pconn)
        {
            gMsgUI.Fatal("Cannot transfer data from memory '%s' from memory '%s' although the mapping says so",
                            pfrommem->Name.c_str(), ptomem->Name.c_str());
            return (Tasknode*) nullptr;
        }
        
        auto newtask = upGraph_->EmplaceNode();
        newtask->Duration = pconn->DmaCost(size);
        
        newtask->Processors.reserve(pconn->Controllers.size());
        for(auto pctrl : pconn->Controllers) newtask->Processors.push_back(pctrl->Index + DmaIndexBase_);
        
        return newtask;
    };
    
    for(auto &dep : Program_.Dependencies)
    {
        auto *pfrommem = pdm->at(dep.From.TheIface), *ptomem = pdm->at(dep.To.TheIface);
        auto *pspillmem = psm->at(&dep);
        assert(pfrommem && ptomem && pspillmem);
            
        if(!pspillmem)
        {
            if(pfrommem == ptomem)
            {
                Transitions.emplace_back(dep, pfrommem->Index);
                continue;
            }
            else
            {
                auto newtask = connect(pfrommem, ptomem, dep.GetMemSize());
                if(!newtask) return false;
                Transitions.emplace_back(dep, newtask, pfrommem->Index);
                Transitions.emplace_back(newtask, dep, ptomem->Index);
                newtask->pTransferDims = &Transitions.back().From.Index;
            }
        }
        else
        {
            auto size = dep.GetMemSize();
            
            auto newtask1 = connect(pfrommem, pspillmem, size), newtask2 = connect(pspillmem, ptomem, size);
            if(!newtask1 || !newtask2) return false;
            
            Transitions.emplace_back(dep, newtask1, pfrommem->Index);
            Transitions.emplace_back(newtask2, dep, ptomem->Index);
            newtask1->pTransferDims = newtask2->pTransferDims = &Transitions.back().From.Index;
            
            auto pe = upGraph_->EmplaceEdge(newtask1, newtask2, nullptr);
            pe->Size = size;
            pe->Mem = pspillmem->Index;
            newtask1->DataDist.resize(1);
            newtask1->DataDist[0].emplace_back();
            auto &u = newtask1->DataDist[0].back();
            u.Size = size;
            pe->FromDist = &u;
        }
    }
    
    return true;
}

// Splits the interfaces into "uses" (data distributions), s.t. 1 "use" is entirely consumed by all targets
void Schedule::CalcDataDist()
{
    std::unordered_map<void*, gen::SpaceMultiDiv<TransitionImpl*>> usemaps;
    
    for(auto &n : upGraph_->Nodes())
    {
        if(n.pSpec)
        {
            n.DataDist.resize(n.pSpec->Ifaces.size());
            for(auto &d : n.pSpec->Ifaces) usemaps.emplace(&d, gen::Space(d.GetDimensions()));
        }
        else if(n.pTransferDims)
        {
            n.DataDist.resize(1);
            usemaps.emplace(&n, *n.pTransferDims);
        }
    }
    
    for(auto &dep : Transitions)
    {
        void *piface = dep.From.TheIface;
        auto &um = usemaps.at(piface ? piface : dep.pSubstNode);
        um.AssignSection(dep.From.Index, &dep);
    }
    
    auto filluses = [](auto &secs, auto &u, auto basesize)
    {
        u.reserve(secs.size());
        for(auto &sec : secs)
        {
            u.emplace_back();
            auto &use = u.back();
            use.Size = sec.second.GetVolume()*basesize;
            use.Uses.reserve(sec.first->size());
            use.Uses.assign(sec.first->begin(), sec.first->end());
            use.RefCount = use.Uses.size();
        }
    };
    
    for(auto &n : upGraph_->Nodes())
    {
        if(n.pSpec)
        {
            auto &ifaces = n.pSpec->Ifaces;
            for(auto i = ifaces.size(); i-- > 0; )
            {
                auto &d = ifaces[i];
                filluses(usemaps.at(&d).GetSections(), n.DataDist[i], d.GetPacket()->GetBaseType().Size);
            }
        }
        else
        {
            auto secs = usemaps.at(&n).GetSections();
            if(secs.empty()) continue;
            filluses(secs, n.DataDist[0], (*secs.begin()->first->begin())->To.TheIface->GetPacket()->GetBaseType().Size);
        }
    }
}

void Schedule::InsertTransitionEdges(const graph::ItemMap<Tasknode*> &nodemap)
{
    for(auto &n : upGraph_->Nodes())
    {
        for(auto &ifacedist : n.DataDist)
        {
            for(auto &u : ifacedist)
            {
                for(auto *pdep : u.Uses)
                {
                    auto *pto = pdep->To.TheIface->GetTask();
                    auto pedge = upGraph_->EmplaceEdge(&n, nodemap[pto], pdep);
                    pedge->FromDist = &u;
                    pedge->Mem = pdep->Mem;
                }
            }
        }
    }
}

bool Schedule::ReverseListScheduling(long (*priority)(Tasknode&))
{
    // Initialize ready list and unfulfilled dependency count
    std::priority_queue<SchedulingItem> readylist;
    for(auto &n : upGraph_->Nodes())
    {
        n.Start = 0;
        n.OpenDependencies = n.OutEdgeCount();
        if(n.OpenDependencies == 0)
        {
            readylist.emplace(n, priority(n));
        }
    }
    for(auto &co : CoreOccs_) co.Clear();
    
    //Start scheduling
    auto lefttoschedule = upGraph_->Nodes().size();
    while(!readylist.empty())
    {
        SchedulingItem next = readylist.top();
        readylist.pop();
        
        auto &tn = *next.pTasknode;
        
        // Find out when we can schedule the task
        Time sched = next.ReadyTime;
        for(auto cid : tn.Processors) sched = std::max(sched, CoreOccs_[cid].Available(sched, tn.Duration, &tn));
        
        // Can we schedule the task now, or not yet?
        if(!readylist.empty() && readylist.top().ReadyTime < sched)
        { //cannot schedule just yet, so put back in list for later
            readylist.emplace(sched, next);
            continue;
        }
        
        //Schedule the task NOW!
        --lefttoschedule;
        
        auto endsched = sched+tn.Duration;
        tn.Start = -endsched;
        if(-endsched > tn.Alap) tn.Alap = -endsched;
        
        for(auto cid : tn.Processors) CoreOccs_[cid].Occupy(sched, endsched, &tn);
        
        // Now check successors on whether they are ready (and when)
        for(auto &e : tn.InEdges())
        {
            auto &pred = *e.GetSource();
            if(endsched > pred.Start) pred.Start = endsched;
            if(--pred.OpenDependencies == 0) readylist.emplace(pred, priority(pred));
        }
    }
    
    if(lefttoschedule > 0)
    {
        gMsgUI.Error("List scheduling failed: Not all tasks could be scheduled.");
        return false;
    }
    
    return true;
}


bool Schedule::CalcAlap()
{
    for(auto &n : upGraph_->Nodes()) n.Alap = -Time_Infinite;
    
    return ReverseListScheduling([](Tasknode &n){ return long(n.OutEdgeCount());})
        || ReverseListScheduling([](Tasknode &n){ return long(n.InEdgeCount());})
        || ReverseListScheduling([](Tasknode &n){ return long(std::rand());})
        || ReverseListScheduling([](Tasknode &n){ return -n.Alap;});
}


bool Schedule::ListScheduling(int weight, IfaceMapping *pdm, SpillMapping *psm, bool prerun)
{
    auto &graph = *upGraph_.get();
    constexpr auto infinite = decltype(CoreOccs_)::value_type::Infinite;
    
    struct MemRequirement
    {
        Tasknode *pTasknode;
        long Amount;
    };
    auto priority = [weight,prerun](Tasknode &tn)
    {
        if(tn.pSpec) return (tn.MemDiff<<weight) - tn.Alap;
        else
        {
            if(prerun) return (tn.TotalMemUse/2<<weight) - tn.Alap;
            else return std::numeric_limits<long>::max()/2 - tn.Alap;
        }
    };
    
    // Initialize ready list and unfulfilled dependency count
    std::priority_queue<SchedulingItem> readylist;
    for(auto &n : graph.Nodes())
    {
        n.Start = 0;
        n.OpenDependencies = n.InEdgeCount();
        if(n.OpenDependencies == 0)
        {
            readylist.emplace(n, priority(n));
        }
        for(auto &dd : n.DataDist) for(auto &u : dd) u.RefCount = u.Uses.size();
    }
    for(auto &co : CoreOccs_) co.Clear();
    for(auto &go : GroupOccs_) go.Clear();
    for(auto &mo : MemOccs_) mo.Clear();

    std::vector<long> memalloc, memfree;
    std::vector<std::vector<MemRequirement>> memrequirements(MemOccs_.size());
    
    //Start scheduling
    auto lefttoschedule = graph.Nodes().size();
    while(!readylist.empty())
    {
        SchedulingItem next = readylist.top();
        readylist.pop();
        
        auto &tn = *next.pTasknode;
        auto *pt = tn.pSpec;
        
        // Find out when we can schedule the task
        Time sched = next.ReadyTime;
        for(auto cid : tn.Processors) sched = std::max(sched, CoreOccs_[cid].Available(sched, tn.Duration, &tn));
        
        if(!pdm)
        { //No transportation tasks without memory mapping, so we only have "real" tasks here
            for(auto *pg : Platform_.GetCores()[tn.Processors.front()].Groups)
            {
                sched = std::max(sched, GroupOccs_[pg->Index].Available(sched, tn.Duration, tn.TotalMemUse));
            }
        }
        else
        {
            memalloc.assign(MemOccs_.size(), 0);
            memfree.assign(MemOccs_.size(), 0);
            if(pt)
            { //trying to schedule a user-specified task (not a transport task)
                for(int i = pt->Ifaces.size(); i-- > 0; )
                {
                    auto &iface = pt->Ifaces[i];
                    if(iface.GetPacket()->GetAccessType() == Packet::in) continue;
                    
                    auto mem = pdm->at(&iface)->Index;
                    auto ifacesize = iface.GetMemSize(); // size of the interface: must be allocated for an output interface
                    auto freesize = ifacesize; // size to free immediately after execution: all the output data...
                    for(auto &u : tn.DataDist[i]) freesize -= u.Size; // ...that is not used later on

                    memfree[mem] += freesize;
                    if(iface.GetPacket()->GetAccessType() == Packet::out) memalloc[mem] += ifacesize;
                }
                
                if(prerun)
                {   // prerun: allocate memory now that normally would have been allocated by the transport task
                    for(auto &e : tn.InEdges()) //TODO: schedule task later if time is still needed for data transport
                    {
                        auto &from = *e.GetSource();
                        if(!from.pSpec && size_t(e.FromDist->RefCount) == e.FromDist->Uses.size())
                        { // from is a transport task, and the memory has not been allocated yet by another task
                            memalloc[e.Mem] += e.Size;
                        }
                    }
                }
            }
            else if(!prerun) 
            { // trying to schedule a transport task, and we are not doing a prerun, so allocate output memory
                for(auto &dd : tn.DataDist)
                    for(auto &u : dd)
                        memalloc[u.Uses.front()->Mem] += u.Size;
            }
            
            for(auto i = memalloc.size(); i-- > 0; )
            {
                if(memalloc[i] > 0) // if we have to allocate memory on module #i, see if (and when) enough is available
                {
                    sched = std::max(sched, MemOccs_[i].Available(sched, infinite, memalloc[i]));
                    if(sched == infinite)
                    {
                        memrequirements[i].push_back(MemRequirement({&tn, memalloc[i]}));
                        break;
                    }
                }
            }
        }

        // Can we schedule the task now, or not yet?
        if(!readylist.empty() && readylist.top().ReadyTime < sched)
        { //cannot schedule just yet, so put back in list for later
            if(sched < infinite) readylist.emplace(sched, next);
            continue;
        }
        if(sched == infinite) break;
        
        //Schedule the task NOW!
        --lefttoschedule;
        tn.Start = sched;
        
        auto endsched = sched+tn.Duration;
        for(auto cid : tn.Processors) CoreOccs_[cid].Occupy(sched, endsched, &tn);
        if(!pdm)
        { //No transportation tasks without memory mapping, so we only have "real" tasks here
            for(auto *pg : Platform_.GetCores()[tn.Processors.front()].Groups)
                GroupOccs_[pg->Index].Occupy(sched, endsched, tn.TotalMemUse);
        }
        else
        {
            for(auto &e : tn.InEdges())
            {
                if(--e.FromDist->RefCount == 0)
                    memfree[e.Mem] += e.Size;
            }
            
            for(auto i = memalloc.size(); i-- > 0; )
            {
                if(memalloc[i] && !MemOccs_[i].Occupy(sched, infinite, memalloc[i])) assert(false);
                if(memfree[i])
                {
                    if(!MemOccs_[i].Unoccupy(endsched, infinite, memfree[i])) assert(false);
                    auto avail = MemOccs_[i][endsched];
                    for(auto &req : memrequirements[i])
                    { // now that we have freed some memory, see if other tasks can be scheduled again
                        if(req.Amount <= avail)
                        {
                            if(endsched > req.pTasknode->Start) req.pTasknode->Start = endsched;
                            readylist.emplace(*req.pTasknode, priority(*req.pTasknode));
                        }
                    }
                }
                
                if(prerun && memalloc[i]) RuntimeOccEnds_[i].emplace(endsched, &tn);
            }
        }
        
        // Now check successors on whether they are ready (and when)
        for(auto &e : tn.OutEdges())
        {
            auto &succ = *e.GetTarget();
            if(endsched > succ.Start) succ.Start = endsched;
            if(--succ.OpenDependencies == 0) readylist.emplace(succ, priority(succ));
        }
    }
    
    if(lefttoschedule > 0)
    {
        gMsgUI.Error("List scheduling failed: Not all tasks could be scheduled.");
        return false;
    }
    
    return true;
}


bool Schedule::CalcSchedule(int weight, IfaceMapping *pdm, SpillMapping *psm)
{
    BuildGraph(pdm, psm);

    auto &graph = *upGraph_.get();
    if(!CalcAlap()) return false;
    
    // Do one first list scheduling run in which buffers "temporarily disappear" between transfer and consumer tasks
    if(!ListScheduling(weight, pdm, psm, true)) return false;
    if(!pdm) return true;
    
    // Check the obtained schedule on when the transfer tasks can be scheduled while respecting memory constraints,
    // add dependencies which ensure this
    for(auto &n : graph.Nodes())
    {
        if(n.pSpec) continue;
        
        auto latealloc = Min(n.OutEdges(), [](auto &e){ return e.GetTarget()->Start; });
        auto mem =n.OutEdgesBegin()->Mem;
        auto earlyalloc =  MemOccs_[mem].AvailableSince(latealloc, n.TotalMemUse/2);
        std::for_each(std::make_reverse_iterator(RuntimeOccEnds_[mem].upper_bound(earlyalloc)),
                      RuntimeOccEnds_[mem].rend(),
                      [&graph, &n](auto &entry) { graph.EmplaceEdge(entry.second, &n, nullptr); });
    }
    
    // Now perform a complete list scheduling with the help of the previously inserted dependencies
    return ListScheduling(weight, pdm, psm, false);
}

graph::ItemMap<Schedule::TaskTimings> Schedule::GetTaskTimings() const
{
    auto ret = Program_.TaskGraph.GetNodeMap<TaskTimings>();
    
    for(auto &n : upGraph_->Nodes())
    {
        if(n.pSpec) ret[n.pSpec] = TaskTimings({n.Start, n.Start+n.Duration, n.Alap-n.Start});
    }
    return ret;
}

}} //namespace Ladybirds::opt


