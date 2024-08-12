// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <unordered_set>
#include <queue>
#include <numeric>
#include <iostream>

#include "graph/graph-extra.h"
#include "graph/itemmap.h"
#include "lua/pass.h"
#include "msgui.h"
#include "kernel.h"
#include "program.h"
#include "task.h"
#include "taskgroup.h"
#include "tools.h"

namespace {

using std::vector;
using Ladybirds::spec::Task;
using Ladybirds::impl::Program;
using Ladybirds::lua::Pass;


bool TaskTopoSort(Program & prog);
Pass TaskTopoSortPass("TaskTopoSort", &TaskTopoSort, Pass::Requires{},
                      Pass::Destroys{"CalcSuccessorMatrix", "LoadMapping", "PopulateGroups"});


/// \internal Sorts \p depgraph topologically and returns the order in \p order.
/// Returns true on success and false if the dependency graph is cyclic.
bool GetTopologicalOrder(const Ladybirds::spec::TaskGraph & tg, /*out*/ vector<const Task *> & order)
{
    // basic idea: gradually "schedule" tasks (i.e. append them to order), starting with tasks without predecessors
    // remove each scheduled task from the graph (of course not in real, we just alter a map called inEdgeCounts)
    // when a node is removed, there may be new nodes without predecessor
    
    auto inEdgeCounts = tg.GetNodeMap<int>(0); //create a map with the incoming edge count for each node
    for(auto & t : tg.Nodes()) inEdgeCounts[t] = t.InEdgeCount();
    
    assert(order.empty());
    order.reserve(tg.Nodes().size());
    
    std::queue<const Task*> candidates; //nodes that are ready to be scheduled
    while(true)
    {
        for(auto & t : tg.Nodes())
        {
            if(inEdgeCounts[t] == 0) //no further incoming edges, so the node is ready to be scheduled
            {
                candidates.push(&t);
                inEdgeCounts[t] = -1; //don't add it twice to the candidate list
            }
        }
        if(candidates.empty()) break;
        
        do
        {
            auto cur = candidates.front(); //select a node and schedule it
            candidates.pop();
            order.push_back(cur);
            
            //now all successors of the node have one dependency fulfilled, i.e. reduce the number of incoming edges
            for(auto & d : cur->OutEdges()) --inEdgeCounts[d.GetTarget()];
            
        } while(!candidates.empty());
    }    
    return order.size() == tg.Nodes().size();
}


//! Evaluates dependencies between the tasks and sorts the task list topologically.
//! If there are cyclic dependencies, an error message is printed and false is returned.
bool TaskTopoSort(Program & prog)
{
    vector<const Task*> topoOrder;
    if(!GetTopologicalOrder(prog.TaskGraph, topoOrder))
    {
        auto & strm = gMsgUI.Error("The program has cyclic dependencies between the tasks.");
        
        auto sccs = StronglyConnected(prog.TaskGraph);
        strm << sccs.size() << " (cyclic) strongly connected components:\n";
        for(auto & scc : sccs)
        {
            for(auto * n : scc) strm << " " << n->Name;
            strm << std::endl;
        }
        strm << std::endl;
        return false;
    }
    
    prog.TaskGraph.ReorderNodes(topoOrder);
    return true;
}
} //namespace ::
