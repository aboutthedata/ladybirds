// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "cacheindexopt.h"

#include <algorithm>
#include <fstream>
#include <stack>
#include <unordered_map>
#include <vector>

#include "../gen/multicritcmp.h"
#include "../graph/edgeregister.h"
#include "../loadstore.h"
#include "../lua/luaenv.h"
#include "../lua/luaload.h"
#include "../msgui.h"
#include "../program.h"
#include "../task.h"
#include "../taskgroup.h"
#include "../buffer.h"

namespace Ladybirds{ namespace opt{

using std::unordered_map;
using impl::Buffer;
using impl::Program;
using spec::Task;

using std::cout;
using std::endl;


namespace {
    class BufferEdge;
    class BufferNode : public graph::Node<graph::Graph<BufferNode>, BufferEdge>
    {
    public:
        impl::Buffer * pBuffer;
        bool Ignore = false;
        int Color = -1;
        
        BufferNode(impl::Buffer * pbuffer) : pBuffer(pbuffer) {}
    };
    
    class BufferEdge : public graph::Edge<BufferNode>
    {
    public:
        int Penalty = 0; //Number of tasks that simultaneously access two buffers source and dest
    };
}//namespace ::

class CacheIndexOpt::BufferRelationGraph : public graph::Graph<BufferNode> {};


CacheIndexOpt::CacheIndexOpt(const spec::Platform::Cluster & clusterinfo, const spec::Platform::CacheConfig & cacheinfo)
            : ClusterInfo_(std::move(clusterinfo)), CacheConfig_(std::move(cacheinfo)) {}
CacheIndexOpt::~CacheIndexOpt(){}


bool CacheIndexOpt::Optimize(impl::TaskDivision &div)
{
    CreateBufferGraph(div);
    return FillBankInfo()
        && RunOptimization();
}



void CacheIndexOpt::CreateBufferGraph(impl::TaskDivision & div)
{
    upBufferGraph_ = std::make_unique<BufferRelationGraph>();
    
    //add nodes, and insert them in a buffer to node map
    graph::ItemMap<BufferNode*> buffermap(div.Buffers);
    for(auto & tr : div.Buffers)
    {
        buffermap[tr] = upBufferGraph_->EmplaceNode(&tr);
    }
    
    //now add penalty edges
    graph::UniEdgeRegister<BufferRelationGraph> edges(upBufferGraph_.get());
    for(auto & pt : div.GetTasks())
    {
        for(auto i1 = pt->Ifaces.size(); i1-- > 0; )
        {
            const Buffer * tr1 = pt->Ifaces[i1].GetBuffer();
            BufferNode * tn1 = buffermap[tr1];
            
            for(auto i2 = i1; i2-- > 0; )
            {
                const Buffer * tr2 = pt->Ifaces[i2].GetBuffer();
                if(tr1 == tr2) continue; //in case of buddies, multiple interfaces can be linked to the same buffer
                
                auto * edge = edges(tn1, buffermap[tr2]);
                edge->Penalty++;
            }
        }
    }
}

//! Initialization: Clear all (possible) bank offset assignments and check if the buffers have all been assigned banks
bool CacheIndexOpt::FillBankInfo()
{
    assert(upBufferGraph_);
    
    const int nbanks = ClusterInfo_.nBanks;
    Banks_.assign(nbanks, BankInfo({ClusterInfo_.BankSize, 0, {}}));
    
    bool ret = true;
    for(auto & tn : upBufferGraph_->Nodes())
    {
        Buffer * ptr = tn.pBuffer;
        if(ptr->MemBank < 0)
        {
            ret = false;
            gMsgUI.Error("Optimizing cache indizes: Buffer %d has not been assigned a memory bank", tn.GetID()-1);
            continue;
        }
        if(ptr->MemBank >= nbanks)
        {
            ret = false;
            gMsgUI.Error("Optimizing cache indizes: Buffer %d has been assigned an invalid memory bank", tn.GetID()-1);
            continue;
        }
        Banks_[ptr->MemBank].FreeSpace -= ptr->Size;
        Banks_[ptr->MemBank].nBuffers++;
        ptr->BankOffset = -1;
    }
    
    return ret;
}


CacheIndexOpt::ColorInfo CacheIndexOpt::GetColors()
{
    auto itmaxdegreenode = std::max_element(upBufferGraph_->NodesBegin(), upBufferGraph_->NodesEnd(), 
                                            [](auto & n1, auto & n2){return n1.EdgeCount() < n2.EdgeCount();});
    
    int ncolors = itmaxdegreenode->EdgeCount() + 1; //safe upper bound
    if(ncolors > CacheConfig_.LineCount)
    {
        gMsgUI.Warning("Too many constraints between buffers; cannot guarantee optimal cache behaviour.");
        return { CacheConfig_.LineCount, CacheConfig_.WordSize, 0};
    }
    else
    {
        int coloroffset = (CacheConfig_.LineCount/ncolors)*CacheConfig_.WordSize;
        constexpr int idealoffset = 256;
        if(coloroffset > idealoffset) coloroffset = idealoffset; //we don't want to waste too much space...
        else gMsgUI.Warning("Many constraints between buffers. Reducing the cache index distances.");
        
        ncolors = CacheConfig_.LineCount*CacheConfig_.WordSize/coloroffset; //if more colors fit into the space, use them
        return {ncolors, coloroffset, CacheConfig_.LineCount*CacheConfig_.WordSize - ncolors*coloroffset};
    }
}

bool CacheIndexOpt::RunOptimization()
{
    std::stack<BufferNode*> stack;
    const int indexmask = (CacheConfig_.LineCount*CacheConfig_.WordSize)-1;
    const auto colors = GetColors();
    
    // 1st step: Delete nodes and put them on the stack (deleting is done by setting the Ignore member to true)
    for(int i = upBufferGraph_->Nodes().count(); i-- > 0; )
    {
        auto nodecmp = Ladybirds::gen::MultiCritCmp(
            [this](BufferNode & n1, BufferNode & n2)
                {return -Banks_[n1.pBuffer->MemBank].FreeSpace + Banks_[n2.pBuffer->MemBank].FreeSpace;},
            [](BufferNode & n1, BufferNode& n2) {return n1.EdgeCount() - n2.EdgeCount();});
        auto lowestDegree = gen::MinElementIf(upBufferGraph_->NodesBegin(), upBufferGraph_->NodesEnd(),
                                                [](auto & n) { return !n.Ignore; }, nodecmp);
        stack.push(&*lowestDegree);
        lowestDegree->Ignore = true;
        //gMsgUI.Verbose("Removing: %d (tasks=%d, neighbours=%ld, penalty=%ld, size=%ld)", lowestDegree->GetID()-1,
        //    lowestDegree->AccessTaskCount, lowestDegree.Neighbours, lowestDegree.Penalty, lowestDegree->pBuffer->Size);
    }
    

    //2nd step: Rebuild the graph step by step and color the nodes
    while(!stack.empty())
    {
        auto pn = stack.top(); stack.pop();
        
        //for each color, find number of conflicts (simultaneously accessed buffers with the same color)
        std::vector<int> conflicts(colors.count);
        for(auto & e : pn->Edges())
        {
            auto pn2 = (e.GetSource() == pn) ? e.GetTarget() : e.GetSource();
            if(pn2->Ignore) continue;
            
            conflicts[pn2->Color]++;
        }
        
        //now find best color: lowest number of conflicts, lowest distance to last buffer, 
        //while it still fits into the bank
        auto & bank = Banks_[pn->pBuffer->MemBank];
        int startpos = 0, pos = 0, color = 0;
        
        auto nextcolor = [&]()//get next color and according address
        {
            pos += colors.offset;
            if(++color >= colors.count)
            {
                color = 0;
                pos += colors.gap;
            }
        };
        
        if(!bank.Slots.empty())
        {
            startpos = bank.Slots.back().End;
            color = ((startpos-1)&indexmask) / colors.offset;
            pos = startpos-1 - (((startpos-1)&indexmask) % colors.offset);
            nextcolor();
        }
        
        int bestcolor = color, bestconflicts = INT_MAX, bestpos = startpos;
        for(int i = colors.count; i > 0; i--)
        {
            if(pos - startpos > bank.FreeSpace) break; //see if it still fits into the bank
            
            if(conflicts[color] < bestconflicts)
            {
                bestcolor = color;
                bestconflicts = conflicts[color];
                bestpos = pos;
            }
            
            nextcolor();
        }
        
        //color has been determined, now assign it
        assert(bestcolor >= 0 && bestcolor < colors.count);
        pn->Color = bestcolor;
        pn->pBuffer->BankOffset = bestpos;
        pn->Ignore = false;
        bank.FreeSpace -= bestpos-startpos;
        bank.Slots.push_back({bestpos, bestpos+pn->pBuffer->Size});
        
        if(bestconflicts > CacheConfig_.Associativity)
            gMsgUI.Warning("Buffer %d: Cache index conflict with %d other buffers (cache associativity: %d)."
                           "This may significantly slow down execution.",
                           pn->GetID()-1, bestconflicts, CacheConfig_.Associativity);
    }
    return true;
}

void CacheIndexOpt::GenerateBufferGraphFile()
{
    std::ofstream bufferGraphFile("cacheindexgraph1.dot");
    using std::endl;

    bufferGraphFile << "graph \"Cache Index Graph\"" << endl << "{" << endl;

    for(auto & n : upBufferGraph_->Nodes())
    {
        bufferGraphFile << "    " << "\"n" << n.GetID()-1 << "\"" << endl;
    }

    bufferGraphFile << endl;

    auto printedge = [&](int i1, int i2)
        { bufferGraphFile << "    \"n" << std::min(i1, i2) << "\"" << " -- \"n" << std::max(i1, i2) << "\""; };
    
    for(auto & e : upBufferGraph_->Edges())
    {
        printedge(e.GetSource()->GetID()-1, e.GetTarget()->GetID()-1);
        bufferGraphFile << " [label=\"" << e.Penalty*2 << "\"]" << endl;
    }

    bufferGraphFile << "}" << endl;

    bufferGraphFile.close();
}
/*
void CacheIndexOpt::PrintAssignmentInfo(std::ostream & strm)
{
    std::vector<std::vector<const BufferNode*>> banks(BankCount_+1); //one additional "virtual bank" for unassigned buffers
    for(auto & tr : upBufferGraph_->Nodes()) banks[tr.pBuffer->MemBank+1].push_back(&tr);

    auto printbank = [&strm](auto & bank)
    {
        size_t sum = 0;
        
        for(auto *tr : bank)
        {
            auto sz = tr->pBuffer->Size;
            sum += sz;
            strm << 'T' << (tr->GetID()-1) << '=' << sz << "; ";
        }
        return sum;
    };
    
    size_t allfree = 0;
    for(int i = 0; i < BankCount_; ++i)
    {
        strm << "Bank " << i << ":\t";
        auto free = InitialBankCapacity - printbank(banks[i+1]);
        strm << "Free: " << free << endl;
        allfree += free;
    }
    strm << "Unassigned:\t";
    auto sum = printbank(banks[0]);
    strm << "(total " << sum << " with " << allfree << " free.)" << endl;
}*/


}} //namespace Ladybirds::opt
