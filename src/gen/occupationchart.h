// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GEN_OCCUPATIONCHART_H
#define LADYBIRDS_GEN_OCCUPATIONCHART_H

#include <assert.h>
#include <algorithm>
#include <limits>
#include <map>

namespace Ladybirds {
namespace gen {

template<typename T>
class SingleOccupation
{
    T *Occupant_;
    
public:
    inline SingleOccupation(T *occupant = nullptr) : Occupant_(occupant) {}
    
    inline int operator+=(const SingleOccupation &other)
        { if(Occupant_) return 2; Occupant_ = other.Occupant_; return 1;}
    inline int operator-=(const SingleOccupation &other)
        { if(Occupant_ != other.Occupant_) return 1; Occupant_ = nullptr; return 0;}
    inline int operator+(const SingleOccupation &) const { return Occupant_ ? 2 : 1; }
    
    inline bool operator==(const SingleOccupation &other) { return Occupant_ == other.Occupant_; }
    
    inline T *Get() { return Occupant_; }
    inline T *operator->() { return Occupant_; }
};
    
/** 
 * Stores the occupation of a certain resource over time
 **/
template<typename T>
class OccupationChart
{
public:
    using Time = long;
    static constexpr Time Infinite = std::numeric_limits<Time>::max();
    
private:
    long Capacity_;
    std::map<Time, T> Entries_;
    
public:
    OccupationChart(long capacity = 1) : Capacity_(capacity), Entries_({{Time(0), T()}}) {}

    T operator[](Time t) const
    {
        assert(t >= 0);
        auto it = Entries_.upper_bound(t);
        return (--it)->second;
    }
    
    void Clear() { Entries_.clear(); Entries_[0] = T(); }
    
    bool Occupy(Time from, Time to, T occ)
    {
        assert(from >= 0 && to > from);
        auto itfrom = Entries_.lower_bound(from); assert(itfrom != Entries_.end());
        auto itto = Entries_.upper_bound(from);
        
        T fromval, toval = std::prev(itto)->second;
        if(itfrom->first != from)
        {
            fromval = itfrom->second;
            if((fromval += occ) > Capacity_) return false;
        }
        
        for(auto it = itfrom; it != itto; ++it)
        {
            if((it->second += occ) > Capacity_)
            {
                do it->second -= occ; while(it-- != itfrom);
                return false;
            }
        }

        if(itfrom->first != from) Entries_.emplace_hint(itfrom, from, fromval);
        else if(itfrom->second == std::prev(itfrom)->second) Entries_.erase(itfrom);
        
        if(itto == Entries_.end() ||  itto->first != to) Entries_.emplace_hint(itto, to, toval);
        else if(itto->second == std::prev(itto)->second) Entries_.erase(itto);
        
        return true;
    }

    bool Unoccupy(Time from, Time to, T occ)
    {
        assert(from >= 0 && to > from);
        auto itfrom = Entries_.lower_bound(from); assert(itfrom != Entries_.end());
        auto itto = Entries_.lower_bound(to);
        
        T fromval, toval = std::prev(itto)->second;
        if(itfrom->first != from)
        {
            fromval = itfrom->second;
            if((fromval -= occ) < Capacity_) return false;
        }
        
        for(auto it = itfrom; it != itto; ++it)
        {
            if((it->second -= occ) < 0)
            {
                do it->second += occ; while(it-- != itfrom);
                return false;
            }
        }

        if(itfrom->first != from) Entries_.emplace_hint(itfrom, from, fromval);
        else if(itfrom->second == std::prev(itfrom)->second) Entries_.erase(itfrom);
        
        if(itto == Entries_.end() ||  itto->first != to) Entries_.emplace_hint(itto, to, toval);
        else if(itto->second == std::prev(itto)->second) Entries_.erase(itto);
        
        return true;
    }
    
    /// Returns the point in time so sooner than \p from at which an amount \p occ of resources
    /// will be available for at least \p duration
    Time Available(Time from, Time duration, T occ) const
    {
        auto it = std::prev(Entries_.upper_bound(from)), itend = Entries_.end();
        while(true)
        {
            it = std::find_if(it, itend, [this, occ](const auto &entry) {return entry.second + occ <= Capacity_;});
            if(it == itend) return Infinite;
            
            if(from < it->first) from = it->first;
            Time to = (duration == Infinite) ? Infinite : from + duration;
            
            it = std::find_if(it, itend, 
                [this, to, occ](auto &entry){return entry.first >= to || entry.second + occ > Capacity_; });
            if(it == itend || it->first >= to) return from;
        }
    }
    
    /// Returns the earliest point t in time such that an amount \p occ of resources is available
    /// throughout the entire timespan [t, \p at). If no such t exists, returns \p at.
    Time AvailableSince(Time at, T occ) const
    {
        auto maxocc = Capacity_ - occ;
        auto revit = std::find_if(std::make_reverse_iterator(Entries_.lower_bound(at)), Entries_.rend(), 
                                  [maxocc](auto &e) {return e.second > maxocc;});
        auto it = revit.base();
        if(it == Entries_.end() || it->first > at) return at;
        return it->first;
    }
    
    /// Returns the maximum amount of resources available throughout the entire timespan [\p from, \p to)
    auto LeastAvail(Time from, Time to) const
    {
        return Capacity_ - std::max_element(std::prev(Entries_.upper_bound(from)), std::prev(Entries_.lower_bound(to)), 
                                            [](auto &a, auto &b) { return a.second < b.second; })->second;
    }
};

/** 
 * Stores the occupation of a certain resource over time, where only one occupant can occupy the resource at a time.
 * A pointer to this occupant is stored.
 **/
template<typename T>
class SingleOccupationChart : public OccupationChart<SingleOccupation<T>>
{
public:
    SingleOccupationChart() = default;  // Capacity is always one, so no need to parametrize that (like in base class)
};


}} //namespace Ladybirds::gen

#endif // LADYBIRDS_GEN_OCCUPATIONCHART_H
