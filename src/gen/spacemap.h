// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GEN_SPACEMAP_H
#define LADYBIRDS_GEN_SPACEMAP_H

#include <deque>

#include "range.h"

namespace Ladybirds {
namespace gen {

template<typename T>
class SpaceMap
{
public:
    struct Entry { Space Key; T Value; };
    using Iterator = typename std::deque<Entry>::iterator;
    
private:
    std::deque<Entry> Entries_;
    
public:
    SpaceMap() = default;
    
    Iterator Begin() { return Entries_.begin(); }
    Iterator End() { return Entries_.end(); }
    
    template<typename... Args>
    Iterator Insert(Args&&... args) { return Entries_.emplace(Entries_.end(), Entry{std::forward<Args>(args)...}); }
    
    void Remove(Iterator pos) { Entries_.erase(pos); }
    
    std::deque<Iterator> FindOverlaps(const Space &s)
    {
        std::deque<Iterator> ret;
        for(Iterator it = Begin(), itend = End(); it != itend; ++it)
        {
            if(it->Key.Overlaps(s)) ret.push_back(it);
        }
        return ret;
    }
};


}} //namespace Ladybirds::gen

#endif // LADYBIRDS_GEN_SPACEMAP_H
