// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef IFACEASSIGNMENT_H
#define IFACEASSIGNMENT_H

#include <memory>
#include <set>
#include <vector>
#include <unordered_map>

#include "opt/common.h"
#include "opt/insertionschedule.h"
#include "opt/schedule.h"
#include "spec/platform.h"


namespace Ladybirds { 

namespace impl { struct Program; }
namespace spec { class Task; class Iface; class Dependency; }
    
namespace opt {
/**
 * Assignment of all interfaces in program to memory modules on a platform
 */
class IfaceAssignment
{
    struct Task;
    struct FullIF;
    class PartIF;
    class InPartIF;
    class OutPartIF;
    class IFGraph;
    class Dependency;

public:
    using IfaceMapping = Schedule::IfaceMapping;
    using SpillMapping = Schedule::SpillMapping;
    using TaskMapping = graph::ItemMap<spec::Platform::Core*>;
    
private:
    const impl::Program &Program_;
    const spec::Platform &Platform_;
    const TaskMapping &TaskMapping_;
    std::unique_ptr<IFGraph> upGraph_;
    
    graph::ItemMap<Schedule::TaskTimings> Timings_;
    std::vector<gen::OccupationChart<long>> MemOccs_, MemPreOccs_;
    std::vector<opt::InsertionSchedule> DmaSchedules_;
    
    
public:
    /// Constructs an empty interface assignment for program \p prog on platform \p pf.
    /** All necessary lists and charts are created, but still empty. **/
    IfaceAssignment(const impl::Program &prog, const spec::Platform &pf, const TaskMapping &mapping);
    ~IfaceAssignment();
    
    bool CalcAssignment(int weight, const Schedule &schedule);
    
private:
    std::vector<FullIF* > CalcAssignmentOrder(int weight, const Schedule& schedule);
    bool EvalAssignment(FullIF& fi, const spec::Platform::HwConnection &conn, Time &finish, Time &succdelaysum, 
                        bool makechanges);
    spec::Platform::ComponentNode * OptMem(FullIF& fi, int& noptions, FullIF*& rpbuddy);
    bool PreAssignment(const std::vector<FullIF*> &order, int weight);
    void ScheduleDMAs(FullIF &fi, spec::Platform::ComponentNode *pmem);
};


}} //namespace Ladybirds::opt

#endif // IFACEASSIGNMENT_H
