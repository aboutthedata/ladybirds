// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GEN_SPACEDIVISION_H
#define LADYBIRDS_GEN_SPACEDIVISION_H

#include <iostream>
#include <unordered_map>

#include "range.h"

namespace Ladybirds { namespace gen {


template<typename AssignType>
class SpaceDivision
{
    using SectionMap = std::unordered_multimap<AssignType, Space>;
    using SearchResult = typename SectionMap::const_iterator;

protected:
    Space FullSpace_;
    SectionMap Sections_;

public:
    SpaceDivision(Space fullspace) : FullSpace_(std::move(fullspace)) {}
    SpaceDivision ( const SpaceDivision &other ) = default;
    SpaceDivision &operator= ( const SpaceDivision &other ) = default;
    
    const Space & GetFullSpace() const { return FullSpace_; }
    const SectionMap & GetSections() const { return Sections_; }
    int GetSectionCount() const { return Sections_.size(); }
    bool IsEmpty() const { return Sections_.empty(); }
    
    void Clear() { Sections_.clear(); }
    
    std::vector<SearchResult> FindOverlaps(const Space & s)
    {
        std::vector<SearchResult> ret;
        
        for(auto it = Sections_.begin(), itend = Sections_.end(); it != itend; ++it)
        {
            if(it->second.Overlaps(s)) ret.push_back(it);
        }
        
        return ret;
    }
    
    //! Assigns all elements in the subspace given by \p sec the value given by \p assign,
    /** possibly overriding or even completely eliminating previous assignments. 
        Note that sec can exceed the full space of this division. **/
    void AssignSection(Space sec, const AssignType & assign)
    {
        sec &= FullSpace_;
        if(sec.IsEmpty()) return;
        
        for(auto & overlap : FindOverlaps(sec))
        {
            TrimSection(overlap, sec);
        }
        Sections_.emplace(assign, sec);
    }
    
    //! Removes all assignments made to \p unassign from this division
    void Unassign(const AssignType & unassign)
    {
        Sections_.erase(unassign);
    }
    
    SpaceDivision SubDivision(const Space & subspace)
    {
        const int ndims = FullSpace_.Dimensions();
        assert(subspace.Dimensions() == ndims);
        SpaceDivision ret(subspace);
        
        Space s;
        for(const auto & secpair : Sections_)
        {
            s = secpair.second & subspace;
            if(!s.IsEmpty()) ret.Sections_.emplace(secpair.first, std::move(s));
        }
        return ret;
    }
    
    Space GetEnvelope(const AssignType &find) const
    {
        auto rg = Sections_.equal_range(find);
        if(rg.first == rg.second)
        {
            auto ret = FullSpace_;
            ret.Clear();
            return ret;
        }
        
        auto ret = rg.first->second;
        for(auto it = rg.first, itend = rg.second; ++it != itend; ) ret |= it->second;
        return ret;
    }
    
protected:
    //! Removes all elements in \p remove from the section denoted by \p ittrim.
    /**This may include splitting it into multiple sections or removing it entirely.**/
    void TrimSection(typename SectionMap::const_iterator ittrim, const Space & remove)
    {
        Space trim = std::move(ittrim->second);
        AssignType assign = std::move(ittrim->first);
        auto ithint = Sections_.erase(ittrim);
        
        Range diff[2];
        for(int i = 0, iend = trim.Dimensions(); i < iend; ++i)
        {
            Range intersec = trim[i] & remove[i];
            int ndiff = RangeSubtract(trim[i], remove[i], diff);
            for(int n = 0; n < ndiff; ++n)
            {
                trim[i] = diff[n];
                ithint = Sections_.emplace_hint(ithint, assign, trim);
            }
            trim[i] = intersec;
        }
    }
};

template<typename AssignType>
std::ostream & operator<<(std::ostream & strm, const SpaceDivision<AssignType> & sd)
{
    strm << "Space division for space " << sd.GetFullSpace() << ":\n";
    for(auto & secpair : sd.GetSections())
    {
        strm << "\t" << secpair.first << "\tto\t" << secpair.second << std::endl;
    }
    return strm;
}


}} //namespace Ladybirds::gen

#endif // LADYBIRDS_GEN_SPACEDIVISION_H
