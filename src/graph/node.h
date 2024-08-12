// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef NODE_H
#define NODE_H

#include <deque>
#include "edge.h"
#include "presdeque.h"


namespace Ladybirds {namespace graph {


template<class GraphClass, class EdgeClass>
class Node : public PresDequeElementBase
{
    template<class, class> friend class Graph;
    
public:
    using graph_t = GraphClass;
    using edge_t = EdgeClass;
private:
    graph_t *Graph_ = nullptr;
    edge_t *FirstInEdge_ = nullptr, *FirstOutEdge_ = nullptr;
    
protected:
    //!Default constructor. Not to be called directly, but only through Graph::EmplaceNode
    inline Node() {}
    inline Node(const Node &) {}; //use with great care only!
    inline Node(Node &&) = default; //dito
    
    inline Node & operator=(const Node &) = delete; // by default
    inline Node & operator=(Node &&) = default;
    
    
public:
    virtual ~Node()                     { Graph_ = nullptr; /*for open::Valid*/ }
    
    inline auto GetGraph()              { return Graph_; }
    inline const graph_t* GetGraph() const  { return Graph_; }
    
    inline auto OutEdgesBegin()         { return OutEdgeIterator<edge_t>(FirstOutEdge_); }
    inline auto OutEdgesEnd()           { return OutEdgeIterator<edge_t>(); }
    inline auto OutEdges()              { return GetItRange(OutEdgesBegin()); }
    inline auto OutEdgesBegin() const   { return OutEdgeIterator<const edge_t>(FirstOutEdge_); }
    inline auto OutEdgesEnd() const     { return OutEdgeIterator<const edge_t>(); }
    inline auto OutEdges() const        { return GetItRange(OutEdgesBegin()); }
    inline int  OutEdgeCount() const    { int i = 0; for([[gnu::unused]] auto &e : OutEdges()) ++i; return i; }
    inline auto InEdgesBegin()          { return InEdgeIterator<edge_t>(FirstInEdge_); }
    inline auto InEdgesEnd()            { return InEdgeIterator<edge_t>(); }
    inline auto InEdges()               { return GetItRange(InEdgesBegin()); }
    inline auto InEdgesBegin() const    { return InEdgeIterator<const edge_t>(FirstInEdge_); }
    inline auto InEdgesEnd() const      { return InEdgeIterator<const edge_t>(); }
    inline auto InEdges() const         { return GetItRange(InEdgesBegin()); }
    inline int  InEdgeCount() const     { int i = 0; for([[gnu::unused]] auto &e : InEdges()) ++i; return i; }
    inline auto EdgesBegin()            { return EdgeIterator<edge_t>(FirstInEdge_, FirstOutEdge_); }
    inline auto EdgesEnd()              { return EdgeIterator<edge_t>(); }
    inline auto Edges()                 { return GetItRange(EdgesBegin()); }
    inline auto EdgesBegin() const      { return EdgeIterator<const edge_t>(FirstInEdge_, FirstOutEdge_); }
    inline auto EdgesEnd() const        { return EdgeIterator<const edge_t>(); }
    inline auto Edges() const           { return GetItRange(EdgesBegin()); }
    inline int  EdgeCount() const       { return InEdgeCount() + OutEdgeCount(); }
};


}} //namespace Ladybirds::graph

#endif // NODE_H
