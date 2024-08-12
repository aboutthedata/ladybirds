// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "range.h"

#include <iostream>
#include <numeric>

namespace Ladybirds { namespace gen {

int RangeSubtract(const Range & from, const Range & sub, /*out*/Range result[2])
{
    if(from.begin() < sub.begin())
    {
        if(from.end() > sub.end())
        {
            result[0] = Range::BeginEnd(from.begin(), sub.begin());
            result[1] = Range::BeginEnd(sub.end(), from.end());
            return 2;
        }
        if(from.end() <= sub.begin())
            result[0] = from;
        else
            result[0] = Range::BeginEnd(from.begin(), sub.begin());
        return 1;
    }
    else //from.begin() >= sub.begin()
    {
        if(from.end() <= sub.end()) return 0;
        
        if(from.begin() >= sub.end())
            result[0] = from;
        else
            result[0] = Range::BeginEnd(sub.end(), from.end());
        return 1;
    }
}
    
bool Range::LoadStoreMembers(loadstore::LoadStore &ls)
{
    int first = Begin_, last = End_-1;
    bool ret = ls.IO("first", first) & ls.IO("last", last);
    if(!ret || ls.IsStoring()) return ret;
    
    if(first > last)
    {
        ls.Error("Invalid range: First comes after last.");
        return false;
    }
    Begin_ = first, End_ = last+1;
    return true;
}

bool Range::LoadFromShortcut(loadstore::LoadStore &ls)
{
    int single;
    if(!ls.RawIO(single)) return false;
    
    Begin_ = single, End_ = single+1;
    return true;
}

std::ostream &operator<<(std::ostream &strm, const Range &r)
{
    switch(r.size())
    {
        case 0:  return strm << "--";
        case 1:  return strm << r.first();
        default: return strm << r.first() << ".." << r.last();
    }
}

    
Space::Space(const std::vector< int > &dimensions)
{
    reserve(dimensions.size());
    for(int dim: dimensions) push_back(Range::BeginCount(0, dim));
}

bool Space::operator==(const Space& other) const
{
    assert(other.size() == size()); //not sure about this assertion.
                                    //But why would one compare spaces with different dimensionality?
    return std::equal(begin(), end(), other.begin());
}

bool Space::Contains(const Space &other) const
{
    assert(other.size() == size());
    return std::equal(begin(), end(), other.begin(), [](const Range & a, const Range & b) { return a.Contains(b); });
}

bool Space::Overlaps(const Space &other) const
{
    assert(other.size() == size());
    return std::equal(begin(), end(), other.begin(), [](const Range & a, const Range & b) { return a.Overlaps(b); });
}

bool Space::IsEmpty() const
{
    return std::any_of(begin(), end(), [](const Range & r){return r.IsEmpty();});
}

int Space::GetVolume() const
{
    return std::accumulate(begin(), end(), 1, [](int i, const Range &r){ return i * r.size(); });
}

void Space::Clear()
{
    for(auto &rg : *this) rg = Range::BeginCount(rg.begin(), 0);
}

Space & Space::operator &=(const Space & s)
{
    assert(s.size() == size());
    if(!std::equal(begin(), end(), s.begin(), [](auto & a, const auto &b) {a &= b; return !a.IsEmpty();})) Clear();
    return *this;
}

Space& Space::operator|=(const Space & s)
{
    assert(s.size() == size());
    std::equal(begin(), end(), s.begin(), [](auto & a, const auto &b) {a |= b; return true;});
    return *this;
}

Space & Space::Remove(const Space &s)
{
    assert(s.size() == size());
    auto res = std::mismatch(begin(), end(), s.begin(), [](auto & a, const auto &b) { return b.Contains(a); });
    if(res.first == end())
    {
        return *this;
    }
    
    if(!std::equal(std::next(res.first), end(), std::next(res.second), 
        [](auto & a, const auto &b) { return b.Contains(a); })) return *this;
    
    res.first->Remove(*res.second);
    return *this;
}

Space& Space::Displace(const std::vector<int> & d)
{
    assert(d.size() == size());
    std::equal(begin(), end(), d.begin(), [](Range & a, int b) {a += b; return true;});
    return *this;
}

Space& Space::DisplaceNeg(const std::vector<int> & d)
{
    assert(d.size() == size());
    std::equal(begin(), end(), d.begin(), [](Range & a, int b) {a -= b; return true;});
    return *this;
}

std::vector<int> Space::GetOrigin() const
{
    std::vector<int> ret; ret.reserve(size());
    for(const Range & r : *this) ret.push_back(r.begin());
    return ret;
}

std::vector<int> Space::GetDimensions() const
{
    std::vector<int> ret; ret.reserve(size());
    for(const Range & r : *this) ret.push_back(r.size());
    return ret;
}

std::vector<int> Space::GetEffectiveDimensions() const
{
    std::vector<int> ret;
    ret.reserve(size());
    for(const Range & r : *this)
    {
        int size = r.size();
        if(size > 1) ret.push_back(size);
    }
    return ret;
}



std::ostream& operator<<(std::ostream& strm, const Space& s)
{
    if(s.Dimensions() == 0) return strm << "( )";
    
    strm << "( " << s[0];
    for(auto it = s.begin(), itend = s.end(); ++it != itend; ) strm << ", " << *it;
    return strm << " )";
}

    
} } //namespace Ladybirds::gen
