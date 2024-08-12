// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GRAPH_ITEMSET_H
#define LADYBIRDS_GRAPH_ITEMSET_H

#include <cstdint>
#include <memory>
#include <vector>

#include "internal/basechecker.h"

namespace Ladybirds {
namespace graph {

template<typename t> class PresDeque;
struct PresDequeElementBase;
    
/** A class to store a subset of items in a list.
 *  This list (called the base of the set) is of type PresDeque.
 *  Internally uses a bit set, such that the memory consumption is minimized
 *  and union and itersection operations are fast.
 **/
class ItemSet
{
private:
    using word = std::size_t;
    static constexpr int wordsize = sizeof(word)*8;
    
private:
    std::vector<word> Vec_;
    std::size_t MinID_;
    std::size_t ElementCountCorrection_, ElementCountCorrectionBackup_;

#ifndef NDEBUG
private:
    //! \internal Used for checking the validity of arguments to member functions. Only available in debug mode.
    std::shared_ptr<internal::BaseCheckerBase> BaseChecker_; 
#endif
    
public:
    ///Constructs an empty (unusable) ItemSet
    inline ItemSet() : MinID_(0) {}
    /// Builds up a set as a subset of \p base. If \p allin is true, the set initially contains all elements in \p base.
    /// Otherwise, the set is initially empty.
    template<typename t> inline ItemSet(const PresDeque<t> & base, bool allin = false);
    
    ItemSet(const ItemSet &) = default;
    ItemSet(ItemSet &&) = default;
    ItemSet & operator=(const ItemSet &) = default;
    ItemSet & operator=(ItemSet &&) = default;

    std::size_t ElementCount() const; ///< Returns the number of elements contained in the set
    
    void Insert(const PresDequeElementBase * pelem); ///< Inserts an item \p pelem into the set
    inline void Insert(const PresDequeElementBase & elem) { Insert(&elem); } ///< Inserts an item \p elem into the set
    void Remove(const PresDequeElementBase * pelem); ///< Removes an item \p pelem from the set
    inline void Remove(const PresDequeElementBase & elem) { Remove(&elem); } ///< Removes an item \p elem from the set
    void InsertAll(); ///< Inserts all items from the base of this set.
    void RemoveAll(); ///< Removes all items from the set.
    
    bool Contains(const PresDequeElementBase * pelem) const; ///<checks if the item \p pelem is contained in the set
    ///checks if the item \p elem is contained in the set
    inline bool Contains(const PresDequeElementBase & elem) const { return Contains(&elem); }

    bool Contains(const ItemSet & other) const; ///< Checks if all items from \p other are contained in this set
    bool Intersects(const ItemSet & other) const; ///< Checks if any item from \p other are contained in this set
    
    
    ItemSet & operator &=(const ItemSet & other); ///< Assigns to this set the intersection of itself and \p other
    ItemSet & operator |=(const ItemSet & other); ///< Assigns to this set the union of itself and \p other
    ItemSet & Remove(const ItemSet &other); ///< Removes from this set all the items contained in \p other
    
    /// Returns the intersection of this set and \p other
    inline ItemSet operator &(const ItemSet & other) { ItemSet ret = *this; return ret &= other; return ret; }
    /// Returns the union of this set and \p other
    inline ItemSet operator |(const ItemSet & other) { ItemSet ret = *this; return ret |= other; return ret; }
};
}} //namespace Ladybirds::graph

#include "itemset.inc"

#endif // LADYBIRDS_GRAPH_ITEMSET_H
