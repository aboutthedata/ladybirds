// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GRAPH_GRAPH_DUMP_H
#define LADYBIRDS_GRAPH_GRAPH_DUMP_H

#include <functional>
#include <iostream>
#include <string>

#include "graph.h"

namespace Ladybirds {
namespace graph {

template<class graphtype> 
void Dump(const graphtype & graph,
          std::ostream & os = std::cout,
          std::function<std::string(const typename graphtype::node_t &)> nodecomments = [](auto &){ return "";},
          std::function<std::string(const typename graphtype::edge_t &)> edgecomments = [](auto &){ return "";})
{
    using std::endl;
    os << "digraph d {" << endl;
    for(auto & node : graph.Nodes())
    {
        os << "\tn" << node.GetID() << " [" << nodecomments(node) <<"];" << endl;
    }
    os << endl;
    
    for(auto & edge : graph.Edges())
    {
        os << "\tn" << edge.GetSource()->GetID() << " -> n" << edge.GetTarget()->GetID() 
           << " [" << edgecomments(edge) <<"];" << endl;
    }
    
    os << "}" << endl;
}

}} //namespace Ladybirds::graph


#endif // LADYBIRDS_GRAPH_GRAPH_DUMP_H
