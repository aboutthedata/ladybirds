// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GRAPH_ITEMMAP_H
#define LADYBIRDS_GRAPH_ITEMMAP_H

#include <memory>
#include <vector>

#include "internal/basechecker.h"

namespace Ladybirds {
namespace graph {

template<typename t> class PresDeque;
struct PresDequeElementBase;


/** A class to store a map from all items in a list to a value of type \p tval.
 *  This list (called the base of the map) is of type PresDeque.
 **/
template<typename tval> class ItemMap
{
private:
    std::vector<tval> Vec_;
    std::size_t MinID_;

#ifndef NDEBUG
private:
    //! \internal Used for checking the validity of arguments to member functions. Only available in debug mode.
    std::shared_ptr<internal::BaseCheckerBase> BaseChecker_; 
#endif
    
public:
    /// Constructs an empty (unusable) ItemMap.
    inline ItemMap() : MinID_(0) {}
    /// Builds up a set as a subset of \p base. If \p allin is true, the set initially contains all elements in \p base.
    /// Otherwise, the set is initially empty.
    template<typename t> ItemMap(const PresDeque<t> & base, tval defaultval = tval());
    
    ItemMap(const ItemMap &) = default;
    ItemMap(ItemMap &&) = default;
    ItemMap & operator=(const ItemMap &) = default;
    ItemMap & operator=(ItemMap &&) = default;
    
    tval & operator [](const PresDequeElementBase & elem);
    inline tval & operator [](const PresDequeElementBase * pelem) { return operator[](*pelem); }
    inline const tval & operator [](const PresDequeElementBase & elem) const
        { return const_cast<ItemMap*>(this)->operator[](elem); }
    inline const tval & operator [](const PresDequeElementBase * pelem) const { return operator[](*pelem); }
    inline bool IsEmpty() const { return MinID_ == 0; }
};
}} //namespace Ladybirds::graph

#include "itemmap.inc"



#endif // LADYBIRDS_GRAPH_ITEMMAP_H
