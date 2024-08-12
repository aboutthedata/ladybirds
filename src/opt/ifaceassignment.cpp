// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "ifaceassignment.h"

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
#include "tools.h"

namespace Ladybirds { namespace opt {

using impl::Program;
using spec::Platform;
using spec::Task;
using spec::Packet;
using spec::Iface;


class IfaceAssignment::PartIF
{
public:
    FullIF &Full;
    Time AvailableFrom = Time_Invalid, AvailableTo = Time_Invalid;
    
public:
    PartIF(FullIF &fi) : Full(fi) {}
};

class IfaceAssignment::OutPartIF : public PartIF
{
public:
    std::vector<InPartIF*> Uses;
    long Size = 0;
    using PartIF::PartIF;
};

class IfaceAssignment::InPartIF : public PartIF
{
public:
    OutPartIF *pDef;
    //const spec::Dependency *pSpec = nullptr;
    int SpillMem = -1;
    int TransferUnit = -1;
    
    InPartIF(OutPartIF *pdef) : PartIF(pdef->Full), pDef(pdef) {}
};

struct IfaceAssignment::FullIF
{
    const spec::Iface &Spec;
    Task &rTask;
    std::vector<InPartIF> InParts;
    std::vector<OutPartIF> OutParts;
    int Size;
    Platform::ComponentNode *pMem = nullptr, *pPremem = nullptr;
    std::vector<FullIF*> Buddies;
    FullIF *BuddySel = nullptr;
    Tristate BuddyUsed = Tristate::Undecided;
    
    FullIF(Task &t, const spec::Iface &d) : Spec(d), rTask(t), Size(d.GetMemSize()) {}
};

struct IfaceAssignment::Task
{
    const spec::Task &Spec;
    std::vector<FullIF> Interfaces;
    Time Start = 0, End = 0, Slack = 0;
    
    Task(const spec::Task &t) : Spec(t)
    {
        Interfaces.reserve(t.Ifaces.size());
        for(auto &d : t.Ifaces) Interfaces.emplace_back(*this, d);
    }
    Task(Task &&) = default;
    Task &operator=(const Task&) = delete;
    Task &operator=(Task&&) = delete;
    void SetTimings(Schedule::TaskTimings &tt) { Start = tt.Start; End = tt.End;  Slack = tt.Slack; }
};


/// \internal Graph representing a program: Tasks have full interfaces, those consist of partial interfaces, those are
/// linked to each other.
class IfaceAssignment::IFGraph
{
public:
    std::vector<Task> Tasks;
    int FullIFCount;
    
public:
    IFGraph(const Program &prog)
    {
        auto &tg = prog.TaskGraph;

        // create tasks
        Tasks.reserve(tg.Nodes().size());
        for(auto &t : tg.Nodes()) Tasks.emplace_back(t);
        
        struct ifinfo {
            FullIF &Iface;
            gen::SpaceMultiDiv<const spec::Dependency*> uses;
            int ndefs = 0;
            
            ifinfo(FullIF &iface) : Iface(iface), uses(gen::Space(iface.Spec.GetDimensions())) {}
        };
        
        // Count full interfaces and create a map with additional infos for construction
        FullIFCount = 0;
        std::unordered_map<const spec::Iface*, ifinfo> ifinfos;
        for(auto &t : Tasks)
        {
            for(auto &d : t.Interfaces)
            {
                ++FullIFCount;
                ifinfos.emplace(&d.Spec, d);
            }
        }
        
        // Determine the partial interfaces
        for(auto &dep : prog.Dependencies)
        {
            auto &ni = ifinfos.at(dep.From.TheIface);
            ni.uses.AssignSection(dep.From.Index, &dep);
        }
        
        // count the number of in partial interfaces for each full interface
        for(auto &entry: ifinfos)
        {
            for(auto &sec : entry.second.uses.GetSections())
            {
                for(auto *pdep : *sec.first) ifinfos.at(pdep->To.TheIface).ndefs++;
            }
        }
    
        // create out partial interfaces, reserve for in partial interfaces
        for(auto &t : Tasks)
        {
            for(auto &fi : t.Interfaces)
            {
                auto &ni = ifinfos.at(&fi.Spec);
                
                fi.InParts.reserve(ni.ndefs);
                
                auto i = ni.uses.GetSectionCount();
                fi.OutParts.reserve(i);
                while(i--) fi.OutParts.emplace_back(fi);
            }
        }
        
        // create in partial interfaces and fill buddy lists
        for(auto &t : Tasks)
        {
            for(auto &fi : t.Interfaces)
            {
                auto basesize = fi.Spec.GetPacket()->GetBaseType().Size;
                
                //in partial interfaces
                auto &ni = ifinfos.at(&fi.Spec);
                auto itout = fi.OutParts.begin();
                for(auto &entry : ni.uses.GetSections())
                {
                    auto &deplist = *entry.first;
                    auto &out = *(itout++);
                    out.Uses.reserve(deplist.size());
                    out.Size = entry.second.GetVolume() * basesize;
                    for(auto *pdep : deplist)
                    {
                        auto &nidest = ifinfos.at(pdep->To.TheIface);
                        nidest.Iface.InParts.emplace_back(&out);
                        out.Uses.push_back(&nidest.Iface.InParts.back());
                    }
                }
                
                //buddy lists
                fi.Buddies.reserve(fi.Spec.GetBuddies().size());
                for(auto &b : fi.Spec.GetBuddies())
                    fi.Buddies.push_back(&ifinfos.at(b).Iface);
            }
        }
    }
};



IfaceAssignment::IfaceAssignment(const Program &prog, const Platform &pf, const TaskMapping &mapping)
    : Program_(prog), Platform_(pf), TaskMapping_(mapping),
      upGraph_(std::make_unique<IFGraph>(prog)),
      DmaSchedules_(pf.GetDmaControllers().size())
{
    MemOccs_.reserve(pf.GetMemories().size());
    for(auto &mem : pf.GetMemories()) MemOccs_.emplace_back(mem.Size*95/100);
}

IfaceAssignment::~IfaceAssignment() {} //For destruction of incomplete type (in class declaration) of Ifacegraph


std::vector<IfaceAssignment::FullIF*> IfaceAssignment::CalcAssignmentOrder(int weight, const Schedule &schedule)
{
    std::vector<FullIF*> ret(upGraph_->FullIFCount, nullptr);
    
    std::vector<Task*> tasks(upGraph_->Tasks.size());
    std::transform(upGraph_->Tasks.begin(), upGraph_->Tasks.end(), tasks.begin(), [](auto &t) {return &t; });
    std::sort(tasks.begin(), tasks.end(), [](auto *t1, auto *t2) { return t1->Slack > t2->Slack; });
    
    auto itret = ret.begin();
    for(auto it = tasks.begin(), itend = tasks.end(); it != itend; )
    {
        auto itretend = itret;
        for(auto slack = (*it)->Slack; it != itend && (*it)->Slack == slack; ++it)
        {
            for(auto &fi : (*it)->Interfaces) *(itretend++) = &fi;
        }
        std::sort(itret, itretend, [](auto *pdn1, auto *pdn2){return pdn1->Size < pdn2->Size;});
        itret = itretend;
    }
    
    return ret;
}

/** \internal Evaluates the option of assigning \p fi to the memory given by \p conn.
 *  Returns true if the assignment is possible, false otherwise. Arguments:
 *  * \p conn: The HwConnection from the core executing the task holding fi to the memory to be evaluated
 *  * \p taskend: [Out] the time at which the task can finish
 *  * \p delaysum [Out] the accumulated time by which the successor task starts are delayed compared to schedule
 *  * \p makechanges: If true, actually carry out the assignment, reserve resources etc. Should only be set to true
 *                    if the function has already succeeded before
 **/
bool IfaceAssignment::EvalAssignment(FullIF& fi, const spec::Platform::HwConnection &memconn,
                                    Time &taskend, Time &succdelaysum, bool makechanges)
{
    auto &connmap = Platform_.GetConnMap();
    auto &iface = fi.Spec;
    auto &task = *iface.GetTask();
    auto &sched = Timings_[task];
    auto pmem = memconn.GetTarget();
    
    // See how much to allocate and if we can use a buddy
    auto size = fi.Size;
    if(fi.BuddyUsed != Tristate::False)
    {
        auto itbuddy = std::find_if(fi.Buddies.begin(), fi.Buddies.end(),
            [pmem](auto *pb){ return pb->pPremem == pmem && pb->BuddyUsed != Tristate::True; });
        auto pbuddy = (itbuddy != fi.Buddies.end()) ? *itbuddy : nullptr;
        if(pbuddy)
        {
            size = std::max(size - pbuddy->Size, 0);
            if(makechanges)
            {
                fi.BuddyUsed = pbuddy->BuddyUsed = Tristate::True;
                fi.BuddySel = pbuddy;
                pbuddy->BuddySel = &fi;
            }
        }
    }
    
    // Calculate when the task can start, i.e. when the packet has been transported to the selected memory
    // On the way, see if we can reuse existing memory, thereby saving more of it
    Time taskstart = 0;
    for(auto &partif : fi.InParts)
    {
        auto *pdef = partif.pDef;
        auto *ptheirmem = pdef->Full.pPremem;
        if(!ptheirmem) continue;
        
        if(ptheirmem == pmem)
        {
            
            continue; //no transport necessary
        }
        
        auto *pconn = connmap[ptheirmem][pmem];
        if(!pconn) return false;
        
        auto transferstart = Timings_[pdef->Full.Spec.GetTask()].End;
        auto transfertime = pconn->DmaCost(pdef->Size);
        //TODO: Handle the case that multiple "in edges" compete for the same DMA controller
        auto dmasched = DmaSchedules_[pconn->Controllers.front()->Index].
                        TryInsertion(transferstart, sched.Start, transfertime);
        taskstart = std::max(taskstart, dmasched.SchedEnd);
    }
    
    if(!makechanges && MemOccs_[pmem->pMem->Index].LeastAvail(sched.Start, sched.End) < size) return false;

    // Now calculate the costs for the task itself, and when it finishes
    Time memcost = iface.Writes*memconn.WriteCost + iface.Reads*memconn.ReadCost;
    Time taskdur =  task.Cost + memcost;
    taskstart = std::max(taskstart, sched.Start); //cannot start sooner due to other possible dependencies
    taskend = taskstart + taskdur;

    // Outputs: If the corresponding interfaces have already been mapped, they are more important and must be optimised
    auto &outconnections = connmap[pmem];
    succdelaysum = 0;
    for(auto &partif : fi.OutParts)
    {
        for(auto &pu : partif.Uses)
        {
            auto *ptheirmem = pu->Full.pPremem;
            if(!ptheirmem) continue;
            
            Time nextstartsched = Timings_[pu->Full.Spec.GetTask()].Start;
            if(ptheirmem != pmem)
            {
                auto *pconn = outconnections[ptheirmem];
                if(!pconn) return false;
                
                //TODO: Handle the case that multiple "out edges" compete for the same DMA controller
                auto dmasched = DmaSchedules_[pconn->Controllers.front()->Index].
                    TryInsertion(taskend, nextstartsched, pconn->DmaCost(partif.Size));
                succdelaysum += std::max<Time>(dmasched.SchedEnd-nextstartsched, 0);
            }
        }
    }
    return true;
}

/// \internal Finds the best-suited memory module for the iface node
Platform::ComponentNode * IfaceAssignment::OptMem(FullIF& fi, int& noptions, FullIF*& rpbuddy)
{
    long soonest_finish = LONG_MAX, least_successors_delay = LONG_MAX;
    Platform::ComponentNode *pmem_selected = nullptr;
    
    auto &iface = fi.Spec;
    auto &task = *iface.GetTask();
    auto &core = *TaskMapping_[task];
    
    noptions = 0;
    for(auto &memconn : core.pNode->OutEdges()) //iterate all possible memories
    {
        Time succdelaysum, taskend;
        if(!EvalAssignment(fi, memconn, taskend, succdelaysum, false)) continue;
        ++noptions;
        
        if(succdelaysum > least_successors_delay) continue;
        if(succdelaysum == least_successors_delay && taskend > soonest_finish) continue;
        
        pmem_selected = memconn.GetTarget();
        least_successors_delay = succdelaysum, soonest_finish = taskend;
    }
    
    return pmem_selected;
}

void IfaceAssignment::ScheduleDMAs(FullIF &fi, Platform::ComponentNode *pmem)
{
    auto &iface = fi.Spec;
    auto &task = *iface.GetTask();
    auto &connmap = Platform_.GetConnMap();
    auto &schedstart = Timings_[task].Start;
    
    // Schedule DMAs for all input edges
    Time taskstart = 0;
    for(auto &piin : fi.InParts)
    {
        auto *pdef = piin.pDef;
        auto *ptheirmem = pdef->Full.pPremem;
        if(!ptheirmem) continue;
        if(ptheirmem == pmem) continue; //no transport necessary
        
        auto *pconn = connmap[ptheirmem][pmem];
        assert(pconn);
        
        auto transferstart = Timings_[pdef->Full.Spec.GetTask()].End;
        auto transfertime = pconn->DmaCost(pdef->Size);
        auto dmacntrl = DmaSchedules_[pconn->Controllers.front()->Index]; //TODO: Multiple DMA controllers
        auto dmasched = dmacntrl.TryInsertion(transferstart, schedstart, transfertime);
        taskstart = std::max(taskstart, dmasched.SchedEnd);
        dmacntrl.PerformInsertion(dmasched);
    }

    // Now calculate when the task finishes
    auto pmemconn = connmap[TaskMapping_[task]->pNode][pmem]; assert(pmemconn);
    Time memcost = iface.Writes*pmemconn->WriteCost + iface.Reads*pmemconn->ReadCost;
    Time taskdur =  task.Cost + memcost;
    taskstart = std::max(taskstart, schedstart); //cannot start sooner due to other possible dependencies
    Time taskend = taskstart + taskdur;

    // Outputs: If the corresponding interfaces have already been mapped, they are more important and must be optimised
    auto &outconnections = connmap[pmem];
    for(auto &partif : fi.OutParts)
    {
        for(auto &pu : partif.Uses)
        {
            auto *ptheirmem = pu->Full.pPremem;
            if(!ptheirmem) continue; //not yet mapped
            if(ptheirmem == pmem) continue; //no transfer necessary
            
            auto *pconn = outconnections[ptheirmem];
            assert(pconn);
            
            auto dmacntrl = DmaSchedules_[pconn->Controllers.front()->Index]; //TODO: Multiple DMA controllers
            Time nextstartsched = Timings_[pu->Full.Spec.GetTask()].Start;
            auto dmasched = dmacntrl.TryInsertion(taskend, nextstartsched,  pconn->DmaCost(partif.Size));
            dmacntrl.PerformInsertion(dmasched);
        }
    }
}

bool IfaceAssignment::PreAssignment(const std::vector<FullIF*> &order, int weight)
{
    for(auto *pfi : order)
    {
        int noptions;
        FullIF * pbuddy;
        auto mem = OptMem(*pfi, noptions, pbuddy);
        if(!mem) return false; //empty map indicates error
        pfi->pPremem = mem;
        if(noptions == 1) pfi->pMem = mem; //already fixed
        
        auto &sched = Timings_[pfi->Spec.GetTask()];
        if(!MemPreOccs_[mem->pMem->Index].Occupy(sched.Start, sched.End, pfi->Size)) assert(false);
        if(noptions == 1 && !MemOccs_[mem->pMem->Index].Occupy(sched.Start, sched.End, pfi->Size)) assert(false);
        ScheduleDMAs(*pfi, mem);
    }
    return true;
}


bool IfaceAssignment::CalcAssignment(int weight, const Schedule &schedule)
{
    Timings_ = schedule.GetTaskTimings();
    auto order = CalcAssignmentOrder(weight, schedule);
    
    // Sort all uses according to scheduled end time of the corresponding task
    for(auto &t : upGraph_->Tasks) for(auto &fi : t.Interfaces) for(auto &opi : fi.OutParts)
    {
        std::sort(opi.Uses.begin(), opi.Uses.end(), [this](auto *pipi1, auto *pipi2)
            { return Timings_[pipi1->Full.Spec.GetTask()].Start < Timings_[pipi2->Full.Spec.GetTask()].Start; });
    }
    
    if(!PreAssignment(order, weight)) return false;
    
    return false;
}


}} //namespace Ladybirds::opt


