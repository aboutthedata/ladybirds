// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GEN_SPACEMULTIDIV_H
#define LADYBIRDS_GEN_SPACEMULTIDIV_H

#include <iostream>
#include <unordered_map>
#include <vector>
#include <set>

#include "range.h"
#include "spacedivision.h"

namespace Ladybirds { namespace gen {

    
/**
 * Like SpaceDivision, but sections can be assigned to multiple objects
 **/
template<typename AssignType>
class SpaceMultiDiv : public SpaceDivision<const std::set<AssignType>*>
{
    using AssignGroup = std::set<AssignType>;
    using Base = SpaceDivision<const AssignGroup*>;
    
private:
    std::set<AssignGroup> AssignGroups_;
    std::unordered_map<AssignType, std::vector<AssignGroup*>> Memberships_;
    
public:
    SpaceMultiDiv(Space fullspace) : Base(std::move(fullspace)) {}
    SpaceMultiDiv (const SpaceMultiDiv &other) = default;
    SpaceMultiDiv &operator= (const SpaceMultiDiv &other) = default;
    
    //! Assigns all elements in the subspace given by \p sec the value given by \p assign,
    /** possibly overriding or even completely eliminating previous assignments. 
        Note that sec can exceed the full space of this division. **/
    void AssignSection(Space sec, const AssignType &assign)
    {
        sec &= this->FullSpace_;
        if(sec.IsEmpty()) return;
        
        SpaceDivision<bool> sd(this->FullSpace_);
        sd.AssignSection(sec, true);
        
        for(auto &overlap : this->FindOverlaps(sec))
        {
            sd.AssignSection(overlap->second, false);
            sd.Unassign(false);

            if(overlap->first->count(assign) == 0)
            {
                this->TrimSection(overlap, sec);
                
                AssignGroup grp = *overlap->first;
                grp.insert(assign);
                auto *pgrp = &*AssignGroups_.insert(std::move(grp)).first;
                this->Sections_.emplace(pgrp, overlap->second & sec);
            }
        }
        
        if(sd.IsEmpty()) return;
        
        auto *pgrp = &*AssignGroups_.insert(AssignGroup({assign})).first;
        for(auto &entry : sd.GetSections()) this->Sections_.emplace(pgrp, entry.second);
    }
    
    //! Removes all assignments made to \p unassign from this division (not implemented currently)
    void Unassign(const AssignType & unassign); //TODO: implement if necessary
    
    SpaceMultiDiv SubDivision(const Space & subspace); // TODO: implement if necessary
};

template<typename AssignType>
std::ostream & operator<<(std::ostream &strm, const typename SpaceMultiDiv<AssignType>::AssignGroup &grp)
{
    const char *sep = "(";
    for(auto &item: grp)
    {
        strm << sep << item;
        sep =", ";
    }
    return strm << ')';
}


}} //namespace Ladybirds::gen

#endif // LADYBIRDS_GEN_SPACEMULTIDIV_H
