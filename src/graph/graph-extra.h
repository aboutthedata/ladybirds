// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GRAPH_GRAPH_EXTRA_H
#define LADYBIRDS_GRAPH_GRAPH_EXTRA_H

#include <algorithm>
#include <limits>
#include <vector>

#include "graph.h"
#include "itemmap.h"
#include "itemset.h"
#include "tools.h"

namespace Ladybirds {
namespace graph {

/// Returns a map of the format (source node, target node) -> edge (or null) for graph \p g
/** Between each pair of nodes in \p g, there must be one edge at most in each direction. **/
template<class graph_t> 
auto EdgeMatrix(graph_t &g)
{
    auto ret = g.GetNodeMap(g.template GetNodeMap<CopyConst<typename graph_t::edge_t, graph_t>*>());
    
    for(auto & edge : g.Edges())
    {
        assert(!ret[edge.GetSource()][edge.GetTarget()]);
        ret[edge.GetSource()][edge.GetTarget()] = &edge;
    }
    
    return ret;
}
    
/// Returns an adjacency matrix for the graph \p g
template<class graph_t> 
ItemMap<ItemSet> AdjacencyMatrix(const graph_t & g)
{
    auto ret = g.GetNodeMap(g.GetNodeSet());
    
    for(auto & edge : g.Edges())
    {
        ret[edge.GetSource()].Insert(edge.GetTarget());
    }
    
    return ret;
}

/// Returns a reachability matrix for the graph \p g, i.e. a map which contains for each node the set of nodes that can
/// be reached from it by following the edges in the graph. Note that the nodes do not list themselves in the matrix.
template<class graph_t> 
ItemMap<ItemSet> ReachabilityMatrix(const graph_t & g)
{
    auto ret = AdjacencyMatrix(g);
    
    //Apply the Floyd-Warshall algorithm to ret, transforming it from an adjacency to a reachability matrix
    for(auto & n1 : g.Nodes())
    {
        for(auto & n2 : g.Nodes())
        {
            if(ret[n1].Contains(n2)) ret[n1] |= ret[n2];
        }
    }
    
    return ret;
}

/// Removes all edges from \p g which are not necessary for keeping the connectivity, i.e. if there was a path from
/// node n1 to node n2 before, there will still be a path from n1 to n2 afterwards.
/// Returns a reachability matrix (cf. \ref ReachabilityMatrix).
template<class graph_t> 
ItemMap<ItemSet> PruneEdges(graph_t & g)
{
    auto ret = AdjacencyMatrix(g), edges = ret;
    
    // Apply the Floyd-Warshall algorithm to ret, transforming it from an adjacency to a reachability matrix
    // At the same time, remove all entries from its copy 'edges' that are considered duplicate, such that 'edges'
    // only contains the edges that are strictly necessary to maintain connectivity.
    for(auto & n1 : g.Nodes())
    {
        for(auto & n2 : g.Nodes())
        {
            if(ret[n1].Contains(n2))
            {
                ret[n1] |= ret[n2];
                edges[n1].Remove(ret[n2]);
            }
        }
    }
    
    for(auto it = g.EdgesBegin(), itend = g.EdgesEnd(); it != itend; )
    {
        auto &edge = *(it++);
        auto &srcout = edges[edge.GetSource()];
        auto *tgt = edge.GetTarget();
        
        if(!srcout.Contains(tgt)) g.RemoveEdge(&edge);
        else srcout.Remove(tgt); //delete entry from list such that we will eliminate subsequent redundant edges
    }
    return ret;
}

/// Returns a vector of strongly connected components (SCCs) in the graph \p g.
/** SCCs containing of only one node are not in the list unless they have a self-edge.
 *  The single nodes without self edges are written to \p psinglenodes, if provided.
 *  
 *  This function uses the algorithm given in doi:10.1016/S0020-0190(00)00051-X: Harold N. Gabow:
 *  "Path-based depth-first search for strong and biconnected components", Information Processing Letters 74, 2000.
 **/
template<class graph_t>
std::vector<std::vector<const typename graph_t::node_t *>> 
StronglyConnected(const graph_t &g, std::vector<const typename graph_t::node_t *> *psinglenodes = nullptr)
{
    using node_t = const typename graph_t::node_t;
    using tag_t = std::size_t;
    constexpr tag_t scctag = std::numeric_limits<tag_t>::max();
    constexpr tag_t singletag = scctag-1;

    struct DfsAlgo
    {
        std::vector<node_t*> path;
        std::vector<tag_t> roots;
        std::vector<std::vector<node_t *>> sccs;
        ItemMap<tag_t> tags;
        
        int singlecnt = 0;
        
        DfsAlgo(const graph_t &g) : tags(g.GetNodeMap(tag_t(0)))
        {
            const auto nodecnt = g.Nodes().size();
            path.reserve(nodecnt);
            roots.reserve(nodecnt);
            sccs.reserve(nodecnt/4);//estimation; we want to minimize the number of reallocations
        }

        void dfs(node_t * n)
        {
            path.push_back(n);
            tags[n] = path.size();
            roots.push_back(path.size());
            bool cycles = false;
            
            for(auto & e : n->OutEdges())
            {
                auto * n1 = e.GetTarget();
                auto n1tag = tags[n1];
                
                if(n1tag == 0) dfs(n1);
                else
                {
                    while(roots.back() > n1tag) roots.pop_back();
                    if(n1 == n) cycles = true;
                }
            }
            
            if(tags[n] == roots.back())
            {
                roots.pop_back();

                if(tags[n] == path.size() && !cycles) //single node
                {
                    ++singlecnt;
                    tags[n] = singletag;
                    path.pop_back();
                }
                else
                {
                    auto rangestart = path.begin()+(tags[n]-1), rangeend = path.end();
                    std::for_each(rangestart, rangeend, [this](auto * pn){tags[pn] = scctag;});
                    sccs.emplace_back(rangestart, rangeend);
                    path.erase(rangestart, rangeend);
                }
            }
        };
    } dfsalgo(g);
    
    auto & tags = dfsalgo.tags;
    
    for(auto & n : g.Nodes())
        if(tags[n] == 0) dfsalgo.dfs(&n);
    
    if(psinglenodes)
    {
        assert(psinglenodes->empty());
        psinglenodes->reserve(dfsalgo.singlecnt);
        for(auto & n : g.Nodes())
        {
            if(tags[n] == singletag) psinglenodes->push_back(&n);
        }
    }
    
    dfsalgo.sccs.shrink_to_fit();
    return dfsalgo.sccs;
}


}} //namespace Ladybirds::graph

#endif // LADYBIRDS_GRAPH_GRAPH_EXTRA_H
