// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef RANGE_H
#define RANGE_H

#include "loadstore.h"

namespace Ladybirds {
namespace gen {


//! Represents a continuous range of integers.
class Range : public loadstore::LoadStorableCompound
{
    int Begin_;
    int End_;

private:
    //! No public constructor with arguments because of different paradigms (begin/end, first/last, first/count).
    //! Use static named functions instead for construction.
    inline Range(int begin, int end) : Begin_( begin ), End_ ( end ) { assert(begin <= end); }
public:
    //! Construct range by first and last element. This range can never be empty (first <= last must hold!).
    inline static Range FirstLast(int first, int last) { assert(first <= last); return Range(first, last+1);}
    //! Construct a range by begin and end (as in C++ standard, end is not part of the range).
    inline static Range BeginEnd(int begin, int end) { assert(begin <= end); return Range(begin, end);}
    //! Construct a range by first element and number of elements.
    inline static Range BeginCount(int begin, int count) { assert(count >= 0); return Range(begin, begin+count);}
    
    //! Constructs an empty range.
    /** No other public constructor exists because of different paradigms (begin/end, first/last, first/count).
     *  Use static named functions instead for construction. **/
    inline Range() : Begin_(0), End_(0) {}
    Range ( const Range &other ) = default;
    Range &operator= ( const Range &other ) = default;
    
    //! Begin (= first element) of the range.
    inline int begin() const { return Begin_; }
    //! First element (=begin) of the range
    inline int first() const { return Begin_; }
    //! End (= integer past last element) of the range
    inline int end() const { return End_; }
    //! Last element of the range
    inline int last() const { return End_ -1; }
    //! Size/length/number of elements in the range
    inline int size() const {return End_ - Begin_;}
    inline bool IsEmpty() const { return Begin_ >= End_; }
    
    //! Standard equality operator
    inline bool operator ==(const Range & other) const { return Begin_ == other.Begin_ && End_ == other.End_; }
    
    //! Returns true if this range and the given range overlap
    inline bool Overlaps(const Range & r) const { return (Begin_ < r.End_) && (End_ > r.Begin_); }
    //! Returns true if the given range is contained in this range
    inline bool Contains(const Range & r) const { return (Begin_ <= r.Begin_) && (End_ >= r.End_); }
    
    //! Sets this range to the union of this and an overlapping range \p r.
    /** Since the return value is supposed to be a range as well, the result will also cover the space between the
        original ranges if those don't overlap. **/
    inline Range & operator |=(const Range & r)
    {
        if(IsEmpty()) return (*this = r);
        if(!r.IsEmpty()) {Begin_ = std::min(Begin_, r.Begin_); End_ = std::max(End_, r.End_);}
        return *this;
    }
    
    //! Sets this range to the intersection of this and another range \p r.
    inline Range & operator &=(const Range & r)
        { Begin_ = std::max(Begin_, r.Begin_); End_ = std::max(Begin_, std::min(End_, r.End_)); return *this; }
    
    //! Removes the intersection of this range and another range \p r from this range.
    /** Since the return value is supposed to be a range as well, nothing will happen if \p r is entirely contained
     *  in this range and does not touch the borders. **/
    inline Range & Remove(const Range &r)
    {
        if(r.Begin_ <= Begin_) Begin_ = std::max(Begin_, std::min(End_, r.End_));
        else if(r.End_ >= End_) End_ = std::min(End_, std::max(Begin_, r.Begin_));
        return *this;
    }
        
    //! Shifts beginning and end of the range by \p offset
    inline Range & operator +=(int offset) { Begin_ += offset; End_ += offset; return *this; }
    //! \copydoc operator +=
    inline Range & operator -=(int offset) { return operator+=(-offset); }
    
    
    virtual bool LoadStoreMembers ( loadstore::LoadStore &ls ) override;
    virtual bool LoadFromShortcut ( loadstore::LoadStore &ls ) override;
};

//! Returns the union of two overlapping ranges \p a and \p b.
/** Since the return value is supposed to be a range as well, \p a and \p b must overlap. 
 *  Otherwise, the result is undefined. **/
inline Range operator |(Range a, const Range & b) { return a |= b; }

//! Returns the intersection of two ranges \p a and \p b.
inline Range operator &(Range a, const Range & b) { return a &= b; }

//! Returns the range \p r, shifted by \p offset
inline Range operator +(Range r, int offset) { return r += offset; }

//! Returns the range \p r, shifted by \p offset
inline Range operator -(Range r, int offset) { return r -= offset; }


//! Removes all the elements in range \p sub from the range \p from and saves the resulting set to \p result.
/** Returns the number of resulting ranges: 0, 1 or 2. 0 Means that the result is the empty set and 2 that two ranges
 *  are necessary to represent the result. \p result must be large enough to hold 2 ranges. **/
int RangeSubtract(const Range & from, const Range & sub, /*out*/Range result[2]);

//! Represents a vector of ranges for multiple dimensions
/**(e.g. for giving the dimensions of a multi-dimensional array or sub-array)**/
class Space : private std::vector<Range>
{
public:
    using vector<Range>::operator[];
    using vector<Range>::begin;
    using vector<Range>::end;
    using vector<Range>::rbegin;
    using vector<Range>::rend;
    using vector<Range>::push_back;
    using vector<Range>::reserve;
    
    Space() = default;
    //! Given d := \p dimensions, constructs a space { [0, d0), [0, d1), ... }.
    explicit Space(const std::vector<int> & dimensions);
    
    bool operator ==(const Space & other) const;
    inline bool operator !=(const Space & other) const { return !operator==(other); }
    
    //! Provides direct access to the underlying vector interface, e.g. for standard serialization functions etc.
    vector<Range> & AsVector() { return *this; }
    //! Provides direct access to the underlying vector interface, e.g. for standard serialization functions etc.
    const vector<Range> & AsVector() const { return *this; }
    
    //! Returns the number of dimensions of the space
    int Dimensions() const { return size(); }
    
    //! Returns true if \p other overlaps this space (i.e. if all element ranges overlap)
    bool Overlaps(const Space & other) const;
    //! Returns true if \p other is contained this space (i.e. if all element ranges are contained)
    bool Contains(const Space & other) const;
    
    //! Returns true if the space does not contain a single point, i.e., if any range in the space is empty
    bool IsEmpty() const;
    
    //! Returns the "volume" occupied by this space, i.e. the product of the sizes of all element ranges
    int GetVolume() const;
    
    //! Sets the space to a zero size, however keeping its origin
    void Clear();
    
    //! Sets this space to the intersection of this and another space \p s.
    Space & operator &=(const Space & s);
    
    //! Sets this space to the union of this and an overlapping space \p s.
    /** Since the return value is supposed to be a space as well, the two spaces must overlap. 
     *  Otherwise, the result is undefined. **/
    Space & operator |=(const Space & s);
    
    //! Removes the intersection of this space and \p s from this space.
    Space & Remove(const Space &s);
    
    //! Shifts the space (i.e. the begins and ends of the ranges) according to vector \p displacement.
    Space & Displace(const std::vector<int> & displacement);

    //! Shifts the space (i.e. the begins and ends of the ranges) into opposite direction of vector \p displacement.
    Space & DisplaceNeg(const std::vector<int> & displacement);
    
    //! Returns a vector with the dimensions of the space (i.e. of the sizes of its ranges).
    //! Ranges of size 1 are collapsed.
    std::vector<int> GetEffectiveDimensions() const;

    //! Returns a vector with the dimensions of the space (i.e. of the sizes of its ranges).
    std::vector<int> GetDimensions() const;
    
    //! Returns a vector with the origin of the space (i.e. of the begins of its ranges).
    std::vector<int> GetOrigin() const;
    
    /** Returns true if \p firstval of all elements of \p first is less than \p secondval of the corresponding
     * elements in \p second.
     * E.g., AllLess<&Range::first, &Range::last>(space1, space2) returns true if space1[i].last() < space2[i].first()
     * for all i.
     \p first and \p second must have the same number of dimensions (i.e. of elements).*/
    template<int (Range::*firstval)() const, int (Range::*secondval)() const>
    static bool AllLess(const Space & first, const Space & second)
    {
        assert(first.size() == second.size());
        return std::equal(first.begin(), first.end(), second.begin(), 
                          [](auto & a, auto &b) {return (a.*firstval)() < (b.*secondval)();});
    }
    /** Returns true if \p firstval of all elements of \p first is greater than \p secondval of the corresponding
     * elements in \p second.
     * E.g., AllGreater<&Range::first, &Range::last>(space1, space2) returns true if space1[i].last() > space2[i].first()
     * for all i.
     \p first and \p second must have the same number of dimensions (i.e. of elements).*/
    template<int (Range::*firstval)() const, int (Range::*secondval)() const>
    static bool AllGreater(const Space & first, const Space & second)
    {
        assert(first.size() == second.size());
        return std::equal(first.begin(), first.end(), second.begin(), 
                          [](auto & a, auto &b) {return (a.*firstval)() > (b.*secondval)();});
    }
};

//! Returns the union of two overlapping spaces \p a and \p b.
/** Since the return value is supposed to be a space as well, \p a and \p b must overlap. 
 *  Otherwise, the result is undefined. **/
inline Space operator |(Space a, const Space & b) { return a |= b; }

//! Returns the intersection of two spaces \p a and \p b.
inline Space operator &(Space a, const Space & b) { return a &= b; }

std::ostream & operator<<(std::ostream & strm, const Range & r);
std::ostream & operator<<(std::ostream & strm, const Space & s);


} //namespace gen
} //namespace Ladybirds
#endif // RANGE_H
