// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GRAPH_EDGEREGISTER_H
#define LADYBIRDS_GRAPH_EDGEREGISTER_H

#include <unordered_map>
#include <utility>
#include "graph.h"

namespace Ladybirds { namespace graph{

struct EdgeRegister__BidirectionalHash
{ 
    static inline uintptr_t hash(void *p1, void *p2)
        { return std::uintptr_t(p1) ^ (std::uintptr_t(p2) << sizeof(p1)*4); }
};

struct EdgeRegister__UnidirectionalHash : private EdgeRegister__BidirectionalHash
{ 
    using bi = EdgeRegister__BidirectionalHash;
    static inline uintptr_t hash(void *p1, void *p2)
        { return p1 < p2 ? bi::hash(p1, p2) : bi::hash(p2, p1); }
};


    
template<class graph_t, class hash = EdgeRegister__BidirectionalHash>
class EdgeRegister
{
    using node_t = typename graph_t::node_t;
    using edge_t = typename graph_t::edge_t;
    
private:
    graph_t *pGraph_;
    std::unordered_map<std::uintptr_t, edge_t*> Map_;
    
public:
    EdgeRegister(graph_t * pgraph) : pGraph_(pgraph)
    {
        for(auto & e : pgraph->Edges())
        {
            Map_[hash::hash(e.GetSource(), e.GetTarget())] = &e;
        }
    }
    
    EdgeRegister(const EdgeRegister&) = delete; //Two registers for same graph don't make sense (synchronization issues)
    EdgeRegister(EdgeRegister &&) = default; //moving is allowed
    EdgeRegister & operator=(const EdgeRegister&) = delete; //see above
    EdgeRegister & operator=(EdgeRegister&&) = default; 
    
    edge_t * Find(node_t* source, node_t * target)
    {
        auto it = Map_.find(hash::hash(source, target));
        return (it == Map_.end()) ? nullptr : *it;
    }
    
    edge_t * operator()(node_t* source, node_t * target)
    {
        edge_t *& e = Map_[hash::hash(source, target)];
        return e ? e : (e = pGraph_->EmplaceEdge(source, target));
    }
};

template<class T> using UniEdgeRegister = EdgeRegister<T, EdgeRegister__UnidirectionalHash>;

}} //namespace Ladybirds::graph


#endif // LADYBIRDS_GRAPH_EDGEREGISTER_H
