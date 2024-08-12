// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "insertionschedule.h"

#include <algorithm>
#include <cassert>
#include <limits>

namespace Ladybirds {
namespace opt {


InsertionSchedule::Job InsertionSchedule::TryInsertion(Time arrival, Time deadline, Time duration) const
{
    constexpr Time infinity = std::numeric_limits<Time>::max();

    auto itend = Jobs_.end();
    //skip all jobs that are finished before the job to insert can start
    auto itstart = std::upper_bound(Jobs_.begin(), itend, arrival,
                                    [](auto a, auto &job) {return a < job.SchedEnd;});
    if(itstart == itend) return {arrival, deadline, arrival, arrival+duration};
    
    //now iterate all possible solutions as to where to insert the new job
    Time tstart = 0, Tbestsched = infinity;
    if(itstart != Jobs_.begin()) tstart = std::prev(itstart)->SchedEnd;
    
    Time maxtotalslack = -1, maxinsertslack = -infinity;
    for(; itstart != itend; ++itstart)
    {
        //start by scheduling job to insert
        Time tsched = std::max(tstart+duration, arrival+duration), t = tsched;
        Time insertslack = deadline-t, totalslack = insertslack;
        if(maxinsertslack > 0 && insertslack < 0) break; // We will not find any better solutions

        // calculate tstart for next iteration, so that below, we can easily "continue" if a solution doesn't work
        auto nextdur = itstart->SchedEnd - itstart->SchedStart;
        tstart = std::max(tstart+nextdur, itstart->Arrival+nextdur);
        bool better; //needed below
        
        // Schedule all the other tasks. If one of them misses its deadline, this solution is not viable
        for(auto it = itstart; it != itend; ++it)
        {
            auto jdur = it->SchedEnd-it->SchedStart;
            t = std::max(t+jdur, it->Arrival+jdur);
            auto jslack = it->Deadline-t;
            if(jslack < 0) goto continue_outer; //cannot allow...
            totalslack += jslack;
        }
        
        better = totalslack > maxtotalslack; //is this solution better than the currently best one?
        if(maxinsertslack < 0)
        {
            if(insertslack < maxinsertslack) better = false;
            if(insertslack > maxinsertslack) better = true;
        }
        
        if(better)
        {
            maxinsertslack = insertslack, maxtotalslack = totalslack;
            Tbestsched = tsched;
        }
        
        continue_outer:;
    }
    
    assert(Tbestsched != infinity);
    Time endsched = Tbestsched + duration;
    return {arrival, std::max(deadline, endsched), Tbestsched, endsched};
}

void InsertionSchedule::PerformInsertion(const Job& job)
{
    auto it = std::upper_bound(Jobs_.begin(), Jobs_.end(), job,
                                [](auto &ins, auto &exist) {return ins.SchedStart < exist.SchedEnd;});
    it = Jobs_.insert(it, job);
    auto itend = Jobs_.end();
    
    Time t = it->SchedEnd;
    while(++it != itend)
    {
        if(t < it->SchedStart) break;
        it->SchedEnd += t - it->SchedStart;
        it->SchedStart = t;
    }
}



}} //namespace Ladybirds::opt
