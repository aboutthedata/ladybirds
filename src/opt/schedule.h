// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <memory>
#include <set>
#include <vector>
#include <unordered_map>

#include "opt/common.h"
#include "gen/occupationchart.h"
#include "spec/platform.h"


namespace Ladybirds { 

namespace impl { struct Program; }
namespace spec { class Task; class Iface; class Dependency; }
    
namespace opt {
/**
 * Schedule of a complete program on the platform
 */
class Schedule
{
    class TransitionImpl;
    class Tasknode;
    class Taskgraph;
    class Dependency;
    struct SchedulingItem;

public:
    using IfaceMapping = std::unordered_map<const spec::Iface*, spec::Platform::Memory *>;
    using SpillMapping = std::unordered_map<const spec::Dependency*, spec::Platform::Memory *>;
    
    static_assert(Time_Infinite == gen::OccupationChart<long>::Infinite, "Different definitions of max time constant");
    
    struct TaskTimings
    {
        Time Start, End, Slack;
    };
    
private:
    impl::Program &Program_;
    spec::Platform &Platform_;
    
    int DmaIndexBase_;
    std::vector<gen::SingleOccupationChart<Tasknode>> CoreOccs_;
    std::vector<gen::OccupationChart<long>> MemOccs_;
    std::vector<gen::OccupationChart<long>> GroupOccs_;
    
    std::vector<std::map<Time, Tasknode*>> RuntimeOccEnds_;
    
    std::unique_ptr<Taskgraph> upGraph_;
    std::vector<TransitionImpl> Transitions;
    
public:
    /// Constructs an empty schedule for program \p prog on platform \p pf.
    /** All necessary lists and charts are created, but still empty. **/
    Schedule(impl::Program &prog, spec::Platform &pf);
    ~Schedule();
    
    bool CalcSchedule(int weight, IfaceMapping *pdm, SpillMapping *psm);
    graph::ItemMap<TaskTimings> GetTaskTimings() const;
    
private:
    void BuildGraph(IfaceMapping *pdm, SpillMapping *psm);
    graph::ItemMap<Schedule::Tasknode*> InsertTaskNodes();
    bool CalcTaskDurations(IfaceMapping *pdm);
    void CalcDataDist();
    bool CalcTransitions(const graph::ItemMap<Tasknode*> &nodemap, IfaceMapping *pdm, SpillMapping *psm);
    void InsertTransitionEdges(const graph::ItemMap<Tasknode*> &nodemap);
    void CalcDependencies();
    bool ReverseListScheduling(long (*priority)(Tasknode&));
    bool CalcAlap();
    bool ListScheduling(int weight, IfaceMapping *pdm, SpillMapping *psm, bool prerun);
};


}} //namespace Ladybirds::opt

#endif // SCHEDULE_H
