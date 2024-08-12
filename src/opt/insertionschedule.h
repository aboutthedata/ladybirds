// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_OPT_INSERTIONSCHEDULE_H
#define LADYBIRDS_OPT_INSERTIONSCHEDULE_H

#include <vector>
#include "opt/common.h"

namespace Ladybirds {
namespace opt {

/**
 * A simple schedule without preemption. Built up by repeatedly inserting new jobs.
 **/
class InsertionSchedule
{
public:
    struct Job
    {
        Time Arrival, Deadline; ///< When is the job ready for execution, and when must it have finished?
        Time SchedStart, SchedEnd; ///< When is the job actually scheduled?
    };
    
private:
    std::vector<Job> Jobs_;
    
public:
    /// Check possibility of inserting a job into the schedule while respecting all deadlines of existing jobs.
    /** If the new job's deadline cannot be met, it will be extended, which can be checked in the return value. **/
    Job TryInsertion(Time arrival, Time deadline, Time duration) const;
    /// Actually perform an insertion that has been analysed with TryInsertion before.
    /** \p job *must* be the exact return value from a previous call to TryInsertion. **/
    void PerformInsertion(const Job &job);
};

}} //namespace Ladybirds::opt

#endif // LADYBIRDS_OPT_INSERTIONSCHEDULE_H
