// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GRAPH_GRAPH_H
#define LADYBIRDS_GRAPH_GRAPH_H

#include <limits>
#include "edge.h"
#include "itemmap.h"
#include "itemset.h"
#include "node.h"
#include "presdeque.h"

namespace Ladybirds {
namespace graph {
    
struct NoVersion
{
    constexpr void operator++() {};
};
class Version
{
    using Valtype = long;
    static constexpr Valtype Uninitialized = std::numeric_limits<Valtype>::lowest();
    static constexpr Valtype Startval = Uninitialized+100; // Try to avoid clashes because of unintended increments
    
    Valtype Val_;
public:
    constexpr Version(bool initialize = false) : Val_(initialize ? Startval : Uninitialized) {}
    
    constexpr void operator++() {++Val_;}
    constexpr bool operator==(Version other) { return other.Val_ == Val_; }
    constexpr bool operator!=(Version other) { return other.Val_ != Val_; }
};

template<class NodeClass, class VersionClass = NoVersion>
class Graph
{
public:
    using node_t  = NodeClass;
    using edge_t  = typename node_t::edge_t;
    using ID_t    = typename PresDeque<node_t>::ID_t;
    
private:
    PresDeque<node_t> Nodes_;
    PresDeque<edge_t> Edges_;
    Version Version_ = Version(true);
    
public:
    using NodeIterator = typename decltype(Nodes_)::iterator;
    using ConstNodeIterator = typename decltype(Nodes_)::const_iterator;
    using EdgeIterator = typename decltype(Edges_)::iterator;
    using ConstEdgeIterator = typename decltype(Edges_)::const_iterator;
    
public:
    Graph() = default;
    Graph(const Graph &) = delete; //by default
    Graph(Graph &&other) { operator=(std::move(other)); };
    
    Graph & operator=(const Graph&) = delete; //by default
    Graph & operator=(Graph &&other)
    {
        Nodes_ = std::move(other.Nodes_), Edges_ = std::move(other.Edges_);
        for(auto &n : Nodes_) n.Graph_ = this;
        ++Version_;
        return *this;
    }
    
    /// Returns the current version number of the graph. This number is changed each time the graph is modified.
    inline Version GetVersion() const { return Version_; }
    
    inline bool IsEmpty() const { return Nodes_.empty(); }
    inline void Clear() { Edges_.clear(); Nodes_.clear(); ++Version_; }
    inline void ClearEdges()
    {
        Edges_.clear();
        for(auto &n : Nodes_) n.FirstInEdge_ = n.FirstOutEdge_ = nullptr;
        ++Version_;
    }
    
    inline auto NodesBegin()       { return Nodes_.begin(); }
    inline auto NodesEnd()         { return Nodes_.end(); }
    inline auto Nodes()            { return GetRange(Nodes_); }
    inline auto NodesBegin() const { return Nodes_.begin(); }
    inline auto NodesEnd()   const { return Nodes_.end(); }
    inline auto Nodes()      const { return GetRange(Nodes_); }
    
    inline auto EdgesBegin()       { return Edges_.begin(); }
    inline auto EdgesEnd()         { return Edges_.end(); }
    inline auto Edges()            { return GetRange(Edges_); }
    inline auto EdgesBegin() const { return Edges_.begin(); }
    inline auto EdgesEnd()   const { return Edges_.end(); }
    inline auto Edges()      const { return GetRange(Edges_); }

    inline auto GetNodeSet(bool allin = false) const { return Nodes_.GetSubset(allin); }
    inline auto GetEdgeSet(bool allin = false) const { return Edges_.GetSubset(allin); }
    template<typename t> inline auto GetNodeMap(t defaultval = t()) const { return ItemMap<t>(Nodes_, defaultval); }
    template<typename t> inline auto GetEdgeMap(t defaultval = t()) const { return ItemMap<t>(Edges_, defaultval); }

    
    template< class... Args >
    node_t * EmplaceNode(Args&&... args)
    {
        auto it = Nodes_.emplace(std::forward<Args>(args)...);
        it->Graph_ = static_cast<typename node_t::graph_t*>(this);
        ++Version_;
        return &*it;
    }
    
    template< class... Args >
    edge_t * EmplaceEdge(node_t * source, node_t * target, Args&&... args)
    {
        assert(source->GetGraph() == this && target->GetGraph() == this);
        auto & e  = *Edges_.emplace(std::forward<Args>(args)...);
        e.Source_ = source;
        e.Target_ = target;
        
        e.PrevOut_ = nullptr;
        e.NextOut_ = source->FirstOutEdge_;
        source->FirstOutEdge_ = &e;
        e.PrevIn_ = nullptr;
        e.NextIn_ = target->FirstInEdge_;
        target->FirstInEdge_ = &e;
        
        ++Version_;
        return &e;
    }
    
    void RemoveNode(node_t * node)
    {
        assert(node->GetGraph() == this);
        for(auto it = node->OutEdgesBegin(), itend = node->OutEdgesEnd(); it != itend; )
            RemoveEdge(&*it++);
        for(auto it = node->InEdgesBegin(), itend = node->InEdgesEnd(); it != itend; )
            RemoveEdge(&*it++);
        
        Nodes_.erase(node);
        ++Version_;
    }
    
    void RemoveEdge(edge_t * edge)
    {
        assert(Edges_.IsValidElement(edge));
        assert(edge->Source_->Graph_ == this && edge->Target_->Graph_ == this);
        
        if(edge->PrevOut_) edge->PrevOut_->NextOut_ = edge->NextOut_;
        else edge->Source_->FirstOutEdge_ = edge->NextOut_;
        if(edge->NextOut_) edge->NextOut_->PrevOut_ = edge->PrevOut_;
        
        if(edge->PrevIn_) edge->PrevIn_->NextIn_ = edge->NextIn_;
        else edge->Target_->FirstInEdge_ = edge->NextIn_;
        if(edge->NextIn_) edge->NextIn_->PrevIn_ = edge->PrevIn_;
        
        Edges_.erase(edge);
        ++Version_;
    }
    
    //! Reorders the nodes in this graph, according to the order given by \p neworder.
    //! \p neworder must be a list containing (possibly const) pointers to all the nodes in this graph.
    //! Each node from this graph must be contained exactly once, no other nodes are allowed.
    template<typename listT> void ReorderNodes(const listT & neworder)
    {
        assert(neworder.size() == Nodes_.size());
        
        //get a map with the new IDs for each node
        auto nodemap = GetNodeMap<std::ptrdiff_t>(0);
        std::ptrdiff_t i = 0;
        for(const node_t * pn : neworder)
        {
            assert(Nodes_.IsValidElement(pn));
            assert(nodemap[pn] == 0);
            nodemap[pn] = ++i;
        }
        
        //get a map with the new source and target IDs for the edges
        struct edgeids{std::ptrdiff_t from, to;};
        auto edgemap = GetEdgeMap<edgeids>();
        for(auto & e : Edges_) edgemap[e] = {nodemap[e.Source_], nodemap[e.Target_]};
        
        //now move the nodes
        decltype(Nodes_) newnodemap;
        for(const node_t * pn : neworder)
        {
            assert(pn->Graph_ == this);
            newnodemap.emplace(std::move(*const_cast<node_t*>(pn)));
        }
        Nodes_ = std::move(newnodemap);
        
        //finally reroute the edges
        for(auto & e : Edges_)
        {
            auto & ids = edgemap[e];
            e.Source_ = &Nodes_.FromID(ids.from);
            e.Target_ = &Nodes_.FromID(ids.to);
        }
        ++Version_;
    }
};

}} //namespace Ladybirds::graph


#endif // LADYBIRDS_GRAPH_GRAPH_H
