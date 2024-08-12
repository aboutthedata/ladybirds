// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GRAPH_INTERNAL_BASECHECKER_H
#define LADYBIRDS_GRAPH_INTERNAL_BASECHECKER_H

#include <cstdint>
#include <vector>

namespace Ladybirds {
namespace graph {

struct PresDequeElementBase;

namespace internal {


/// \internal a class for checking whether the object passed to an ItemMap/ItemSet method really 
/// belongs to the base of that map/set. Mainly for debugging purposes.
struct BaseCheckerBase
{
    virtual bool Check(const PresDequeElementBase *) = 0;
    virtual bool Check(const BaseCheckerBase *) = 0;
    virtual ~BaseCheckerBase(){}
};
template<typename t> class BaseChecker;


}}} //namespace Ladybirds::graph::internal

#endif // LADYBIRDS_GRAPH_INTERNAL_BASECHECKER_H
