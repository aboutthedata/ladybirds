// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <numeric>
#include <unordered_map>
#include <vector>

#include "gen/multicritcmp.h"
#include "graph/graph.h"
#include "graph/itemset.h"
#include "lua/pass.h"
#include "msgui.h"
#include "program.h"
#include "task.h"
#include "taskgroup.h"
#include "buffer.h"

using Ladybirds::impl::Buffer;
using Ladybirds::graph::ItemSet;
using Ladybirds::spec::Task;
using Ladybirds::spec::TaskGraph;
using Ladybirds::impl::TaskDivision;
using Ladybirds::impl::Program;
using Ladybirds::lua::Pass;


namespace {
bool BufferAllocation(Program & prog);
Pass BufferAllocationPass("BufferAllocation", &BufferAllocation,
                           Pass::Requires{"BufferPreallocation", "CalcSuccessorMatrix", "PopulateGroups"});


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Class definitions for Buffer graph
//

class BufferNode;
class BufferConflict;
using BufferGraph = Ladybirds::graph::Graph<BufferNode>;

class BufferNode : public Ladybirds::graph::Node<BufferGraph, BufferConflict>
{
public:
    const Buffer * pBuffer;
    Buffer * pFinalBuffer = nullptr;
    ItemSet Accesses;
    std::vector<const Task*> LastAccesses;
    
public:
    BufferNode(const Buffer * pbuffer, const TaskGraph & tg)
        : pBuffer(pbuffer), Accesses(tg.GetNodeSet()) {}
};

class BufferConflict : public Ladybirds::graph::Edge<BufferNode>
{
public:
    //Ladybirds::spec::Dependency * pDependency;
    
public:
    BufferConflict(){}//Ladybirds::spec::Dependency * dep) : pDependency(dep) {}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction of a buffer graph
//
bool AddBufferGraphNodes(BufferGraph &g, TaskDivision &div, const TaskGraph &taskgraph)
{
    // a map from the buffers to the corresponding nodes
    Ladybirds::graph::ItemMap<BufferNode*> buffermap (div.Buffers, nullptr);

    //add nodes to graph and fill the access sets of the buffer nodes
    for(auto *ptask : div.GetTasks())
    {
        for(auto &iface : ptask->Ifaces)
        {
            auto *ptr = iface.GetBuffer();
            if(ptr->pExternalSource) continue; //don't merge external buffers
            if(!div.Buffers.IsValidElement(ptr))
            {
                gMsgUI.Error("Buffers spanning across task divisions. "
                    "Ensure communication tasks were properly inserted.");
                return false;
            }
            
            auto *& ptn = buffermap[ptr];
            if(!ptn) ptn = g.EmplaceNode(ptr, taskgraph);
            
            ptn->Accesses.Insert(ptask);
        }
    }
    return true;
}

void FillLastAccesses(BufferGraph & g,  const std::vector<Task*> &tasks, const TaskGraph & taskgraph,
                      const Program::ReachabilityMap & reachmap)
{
    std::vector<const Task*> accesses;
    auto lastaccesses = taskgraph.GetNodeSet();
    
    auto succCounts = taskgraph.GetNodeMap(size_t());
    for(auto & t : taskgraph.Nodes()) succCounts[t] = reachmap[t].ElementCount();
    
    for(auto & trn : g.Nodes())
    {
        accesses.clear();
        lastaccesses.RemoveAll();

        for(auto * t : tasks) if(trn.Accesses.Contains(t)) accesses.push_back(t);
        
        std::sort(accesses.begin(), accesses.end(), 
                  [&succCounts](auto & t1, auto & t2){ return succCounts[t1] < succCounts[t2]; });

        auto it = accesses.begin();
        size_t nsuccsmin = succCounts[*it];
        do
        {
            lastaccesses.Insert(*it);
            trn.LastAccesses.push_back(*it);
        }
        while(++it != accesses.end() && succCounts[*it] == nsuccsmin);
        
        for(; it != accesses.end(); ++it)
        {
            if(!reachmap[*it].Intersects(lastaccesses))
            {
                lastaccesses.Insert(*it);
                trn.LastAccesses.push_back(*it);
            }
        }
    }
}

bool AllBefore(BufferNode & tn1, BufferNode & tn2, const Program::ReachabilityMap & reachmap)
{
    for(const Task * pt : tn1.LastAccesses)
    {
        if(!reachmap[pt].Contains(tn2.Accesses)) return false;
    }
    return true;
}

bool HasConflicts(BufferNode & tn1, BufferNode & tn2,
                  const TaskGraph & taskgraph, const Program::ReachabilityMap & reachmap)
{
    if(AllBefore(tn1, tn2, reachmap) || AllBefore(tn2, tn1, reachmap)) return false;
    
    //TODO: some more sophisticated analysis
    
    return true;
}


void AddBufferGraphEdges(BufferGraph & g, const TaskGraph & taskgraph, const Program::ReachabilityMap & reachmap)
{
    for(auto it1 = g.NodesBegin(), itend = g.NodesEnd(); it1 != itend; ++it1)
    {
        for(auto it2 = it1; ++it2 != itend; )
        {
            if(HasConflicts(*it1, *it2, taskgraph, reachmap)) g.EmplaceEdge(&*it1, &*it2);
        }
    }
}


bool BuildBufferGraph(Program &prog, TaskDivision &div, BufferGraph &g)
{
    if(!AddBufferGraphNodes(g, div, prog.TaskGraph)) return false;
    FillLastAccesses(g, div.GetTasks(), prog.TaskGraph, prog.TaskReachability);
    AddBufferGraphEdges(g, prog.TaskGraph, prog.TaskReachability);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// buffer allocation functionality for one division
//
bool AllocateBuffers(Program & prog, TaskDivision & div)
{
    BufferGraph g;
    if(!BuildBufferGraph(prog, div, g)) return false;
    
    // First step: Determine order in which the nodes are to be colored.
    // Approach here: Determine node with smallest degree. Put it at the end of the list and remove it
    // from the graph. Then determine the next node with the smallest degree in the graph and put it one position 
    // before the end of the list, remove it from the graph and so on.
    std::vector<BufferNode*> order; order.reserve(g.Nodes().size());
    auto edgecounts = g.GetNodeMap(int());
    for(auto & n : g.Nodes())
    {
        order.push_back(&n);
        edgecounts[n] = n.EdgeCount();
    }
    
    for(auto it = order.rbegin(), itend = order.rend(); it != itend; ++it )
    {
        std::partial_sort(it, it+1, itend,
                          [&edgecounts](auto pn1, auto pn2){return edgecounts[pn1] < edgecounts[pn2];});
        
        for(auto & e : (*it)->InEdges())  --edgecounts[e.GetSource()];
        for(auto & e : (*it)->OutEdges()) --edgecounts[e.GetTarget()];
    }


    // Second step: Go through the list of nodes (beginning with the last one, i.e. "most" neighbors) and assign each
    // one a color (i.e. one of the final, "merged" buffers, which are to be allocated in the end).
    // If no such buffer exists that would fit, create a new one.
    Program::BufferList newbuffers;
    std::deque<std::deque<int>> bufferaccesses;
    for(auto * pn : order)
    {
        // Which colors are valid, i.e. not taken by a neighbor node?
        auto valid = newbuffers.GetSubset(true);
        auto updatevalid = [&valid](BufferNode * ptn) { if(ptn->pFinalBuffer) valid.Remove(ptn->pFinalBuffer); };
        for(auto & e : pn->OutEdges()) updatevalid(e.GetTarget());
        for(auto & e : pn->InEdges()) updatevalid(e.GetSource());
        
        auto refsize = pn->pBuffer->Size;
        auto refid = pn->LastAccesses[0]->GetID();
        auto proximity = [refid, &bufferaccesses](Buffer & tr)
        {
            return std::min_element(bufferaccesses[tr.GetID()-1].begin(), bufferaccesses[tr.GetID()-1].end(), 
                [refid](int i1, int i2){ return std::abs(i1-refid) < std::abs(i2-refid); });
        };
        auto nodecmp = Ladybirds::gen::MultiCritCmp(
            [refsize](Buffer & t1, Buffer & t2)
                {return std::abs(t1.Size-refsize) - std::abs(t2.Size-refsize);},
            [refid, proximity](Buffer & t1, Buffer & t2)
                {return proximity(t1) < proximity(t2);});
                //as a minor criterium: assign larger buffers first to avoid "dead end" situations later on
        auto itselect = Ladybirds::gen::MinElementIf(newbuffers.begin(), newbuffers.end(),
                                                [&valid](auto & tr) { return valid.Contains(tr); }, nodecmp);

        if(itselect == newbuffers.end())
        {
            pn->pFinalBuffer = &*newbuffers.emplace(*pn->pBuffer); //use this buffer as a template for the new one
            bufferaccesses.emplace_back();
        }
        else
        {
            pn->pFinalBuffer = &*itselect;
            if(pn->pBuffer->Size > itselect->Size) itselect->Size = pn->pBuffer->Size;
        }
        bufferaccesses[pn->pFinalBuffer->GetID()-1].push_back(refid);
    }
    
    auto bufferstats = [](Program::BufferList & tl)
    {
        auto totalbytes = std::accumulate(tl.begin(), tl.end(), size_t(0),
                                          [](auto s, auto & t){return t.pExternalSource ? s :  s+t.Size;});
        std::cout << tl.size() << " Buffers, in total " << totalbytes << " bytes";
    };
    
    std::cout << "Buffer merging statistics:\n\tbefore:";
    bufferstats(div.Buffers);
    std::cout << "\n\tafter:";
    bufferstats(newbuffers);
    std::cout << std::endl;
    
    Ladybirds::graph::ItemMap<Buffer*> old2new(div.Buffers);
    for(auto & n : g.Nodes()) old2new[n.pBuffer] = n.pFinalBuffer;

    for(auto *ptask : div.GetTasks())
    {
        for(auto & iface : ptask->Ifaces)
        {
            auto *ptr = iface.GetBuffer();
            if(!ptr->pExternalSource) iface.RelocateBuffer(old2new[ptr]);
        }
    }
    div.Buffers = std::move(newbuffers);
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main buffer allocation function
//

bool BufferAllocation(Program & prog)
{
    for(auto & div : prog.Divisions)
    {
        if(!AllocateBuffers(prog, div)) return false;
    }
    return true;
}

} // anonymous namespace
