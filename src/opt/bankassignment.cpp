// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "bankassignment.h"

#include <algorithm>
#include <fstream>
#include <stack>
#include <unordered_map>
#include <vector>

#include "gen/multicritcmp.h"
#include "graph/edgeregister.h"
#include "lua/luaenv.h"
#include "lua/luaload.h"
#include "msgui.h"
#include "program.h"
#include "task.h"
#include "taskgroup.h"
#include "tools.h"
#include "buffer.h"

namespace Ladybirds{ namespace opt{

using std::unordered_map;
using impl::Buffer;
using impl::Program;
using spec::Task;

using std::cout;
using std::endl;



class BufferEdge;
class BufferNode : public graph::Node<graph::Graph<BufferNode>, BufferEdge>
{
public:
    impl::Buffer * pBuffer;
    bool Ignore = false;
    int AccessTaskCount = 0;
    
    BufferNode(impl::Buffer * pbuffer) : pBuffer(pbuffer) {}
};

class BufferEdge : public graph::Edge<BufferNode>
{
public:
    int Penalty = 0;
    int GroupPenalty = 0;
    int Reward = 0;
};

class BufferRelationGraph : public graph::Graph<BufferNode> {};



static int CountPenaltyEdges(BufferNode * pn, long & penalties)
{
    int count = 0;
    penalties = 0;
    for(auto & e : pn->OutEdges())
    {
        if(!e.GetTarget()->Ignore && e.Penalty > 0) {++count; penalties += e.Penalty; }
    }
    for(auto & e : pn->InEdges())
    {
        if(!e.GetSource()->Ignore && e.Penalty > 0) {++count; penalties += e.Penalty; }
    }
    return count;
}

#define ANNOUNCE_ASSIGNMENT(type, pn, extramsg, ...) \
    gMsgUI.Verbose(type  " assignment: %d -> %d" extramsg, pn->GetID()-1, pn->pBuffer->MemBank __VA_ARGS__)





BankAssignment::BankAssignment(impl::Program &prog, int nbanks)
  : Prog_(prog), BankCount_(nbanks), upBufferGraph_(std::make_unique<BufferRelationGraph>())
{
}

BankAssignment::~BankAssignment() {}

class TaskTiming : public loadstore::LoadStorableCompound
{
public:
    std::string TaskName;
    spec::Task * pTask = nullptr;
    long Start, Stop;
    virtual bool LoadStoreMembers(loadstore::LoadStore & ls)
    {
        double dstart, dstop;
        if(!( ls.IO("task", TaskName)
            & ls.IO("start", dstart, true, 0, /*min=*/0)
            & ls.IO("stop", dstop, true, 0, /*min=*/0))) return false;
        Start = dstart, Stop = dstop;
        return true;
    }
};

bool Ladybirds::opt::BankAssignment::LoadOverlaps(const char* filename)
{
    //load timings from lua file
    lua::LuaEnv lua;
    if(!lua.DoFile(filename)) return false;

    lua::LuaLoad load(lua);
    lua_pushglobaltable(lua);

    std::vector<TaskTiming> timings;
    if(!load.IO("Timings", timings)) return false;
    
    //fill the pTask pointers (from the task name strings)
    {
        unordered_map<std::string, spec::Task*> tasks;
        for(auto & t : Prog_.GetTasks()) tasks[t.Name] = &t;
        for(auto & ti : timings)
        {
            if(!(ti.pTask = tasks[ti.TaskName])) gMsgUI.Warning("Task not found: %s", ti.TaskName.c_str());
        }
    }
    auto it = std::remove_if(timings.begin(), timings.end(), [](const auto & ti){return !ti.pTask;});
    timings.erase(it, timings.end());
    
    //determine overlap for each task pair and, if bigger than threshold, add the pair to the overlap list
    constexpr long threshold = 200;
    TaskOverlaps_.clear();
    TaskOverlaps_.reserve(timings.size() * 8); //already reserve memory for 16 overlaps in average per task
    for(auto it1 = timings.begin(), itend = timings.end(); it1 != itend; ++it1)
    {
        for(auto it2 = timings.begin(); it2 != it1; ++it2)
        {
            TaskTiming &t1 = *it1, &t2 = *it2;
            auto overlap = std::min(t1.Stop, t2.Stop) - std::max(t1.Start, t2.Start);
            if(overlap > threshold)
            {
                TaskOverlap o = {t1.pTask, t2.pTask, (unsigned long) overlap};
                TaskOverlaps_.push_back(std::move(o));
            }
        }
    }
    
    if(gMsgUI.IsVerbose())
    {
        std::ofstream strm("ov.txt");
        for(auto & ov : TaskOverlaps_)
        {
            strm << "    \"" << ov.Task1->Name << "\" -- \"" << ov.Task2->Name << "\" [label=\"" << ov.Overlap << "\"]\n";
        }
    }
    return true;
}

void BankAssignment::CreateBufferGraph(impl::TaskDivision &div)
{
    upBufferGraph_->Clear();
    
    //add nodes, and insert them in a buffer to node map
    graph::ItemMap<BufferNode*> buffermap(div.Buffers);
    for(auto & tr : div.Buffers)
    {
        buffermap[tr] = upBufferGraph_->EmplaceNode(&tr);
    }
    
    //now add penalty edges
    graph::UniEdgeRegister<BufferRelationGraph> edges(upBufferGraph_.get());
    for(auto & ov : TaskOverlaps_)
    {
        bool punishgroup = (ov.Task1->Group->GetID() % 8 == ov.Task2->Group->GetID() % 8);
        for(auto &d1 :ov.Task1->Ifaces)
        {
            Buffer * tr1 = d1.GetBuffer();
            BufferNode * tn1 = buffermap[tr1];
            for(auto & d2 : ov.Task2->Ifaces)
            {
                Buffer * tr2 = d2.GetBuffer();
                if(tr1 == tr2) continue;
                
                auto * edge = edges(tn1, buffermap[tr2]);
                edge->Penalty += ov.Overlap;
                if(punishgroup) edge->GroupPenalty += ov.Overlap;
            }
        }
    }
    
    //add reward edges and access counts
    for(auto *pt : div.GetTasks())
    {
        for(auto it1 = pt->Ifaces.begin(), itend = pt->Ifaces.end(); it1 != itend; ++it1)
        {
            BufferNode * tn1 = buffermap[it1->GetBuffer()];
            tn1->AccessTaskCount++;
            for(auto it2 = pt->Ifaces.begin(); it2 != it1; ++it2)
            {
                BufferNode * tn2 = buffermap[it2->GetBuffer()];
                edges(tn1, tn2)->Reward++;
            }
        }
    }
}

void BankAssignment::GenerateBufferGraphFile()
{
    assert(upBufferGraph_ && "Has CreateBufferGraph() been called before GenerateBufferGraphFile()?");
    
    std::ofstream bufferGraphFile(strprintf("buffergraph%d.dot", ++DumpCounter_));
    using std::endl;

    bufferGraphFile << "graph \"Buffer Graph\"" << endl << "{" << endl;

    for(auto & n : upBufferGraph_->Nodes())
    {
        bufferGraphFile << "    " << "\"n" << n.GetID()-1 << "\"" << endl;
    }

    bufferGraphFile << endl;

    auto printedge = [&](int i1, int i2)
        { bufferGraphFile << "    \"n" << std::min(i1, i2) << "\"" << " -- \"n" << std::max(i1, i2) << "\""; };
    
    for(auto & e : upBufferGraph_->Edges())
    {
        if(e.Penalty > 0)
        {
            printedge(e.GetSource()->GetID()-1, e.GetTarget()->GetID()-1);
            bufferGraphFile << " [label=\"" << e.Penalty*2 << "\"]" << endl;
        }
        
        if(e.GroupPenalty > 0)
        {
            printedge(e.GetSource()->GetID()-1, e.GetTarget()->GetID()-1);
            bufferGraphFile << " [label=\"" << e.GroupPenalty*2 << "\", color=red, fontcolor=red]" << endl;
        }

        if(e.Reward > 0)
        {
            printedge(e.GetSource()->GetID()-1, e.GetTarget()->GetID()-1);
            bufferGraphFile << " [label=\"" << e.Reward << "\", color=gray, fontcolor=gray]" << endl;
        }
    }

    bufferGraphFile << "}" << endl;

    bufferGraphFile.close();
}

bool BankAssignment::AssignBanks(int correction)
{
    assert(upBufferGraph_ && "Has CreateBufferGraph() been called before AssignBanks()?");
    
    const int nbanks = BankCount_;
    bool ret = true;
    std::stack<BufferNode*> stack;
    
    // Initialization: Clear all (possible) buffer assignments and check if the buffers are not too big
    long totalsize = 0;
    for(auto & node : upBufferGraph_->Nodes())
    {
        auto &tr = *node.pBuffer;
        tr.MemBank = -1;
        if(tr.Size > InitialBankCapacity)
        {
            ret = false;
            gMsgUI.Error("Buffer is too big to fit in any memory bank (%ld Bytes).", tr.Size);
        }
        totalsize += tr.Size;
    }
    if(!ret) return false;
    
    if(totalsize > BankCount_*InitialBankCapacity)
    {
        gMsgUI.Error("Insufficient memory on the target platform. Program demands %ld Bytes.", totalsize);
        return false;
    }
    if(totalsize > BankCount_*InitialBankCapacity*0.9 && correction == 0)
    {
        gMsgUI.Warning("Program is using more than 90%% of the memory on the platform. This may be hard to map.");
    }

    // 1st step: Delete nodes and put them on the stack (deleting is done by setting the Ignore member to true)
    for(int i = upBufferGraph_->Nodes().count(); i-- > 0; )
    {
        using nodeit = BufferRelationGraph::NodeIterator;
        struct NodeCharacter : public nodeit
        {
            long Neighbours; long Penalty;
            NodeCharacter(const nodeit & it) : nodeit(it) { update(); }
            auto & operator++() { update(); nodeit::operator++(); return *this; }
            auto & operator*() { return *this; }
            auto * get() { return &nodeit::operator*(); }
            void update() { Neighbours = CountPenaltyEdges(get(), Penalty); }
        };
        
        auto nodecmp = Ladybirds::gen::MultiCritCmp(
            [correction](NodeCharacter & b1, NodeCharacter & b2)
                {return -b1->AccessTaskCount - (-b2->AccessTaskCount) + 
                        ((b1->pBuffer->Size - b2->pBuffer->Size)<<correction)/InitialBankCapacity;},
            [](NodeCharacter & b1, NodeCharacter & b2) {return b1.Neighbours - b2.Neighbours;},
            [](NodeCharacter & b1, NodeCharacter & b2) {return b1.Penalty - b2.Penalty;},
            [](NodeCharacter & b1, NodeCharacter & b2) {return b1->pBuffer->Size - b2->pBuffer->Size;});
                //as a minor criterium: assign larger buffers first to avoid "dead end" situations later on
        auto lowestDegree = gen::MinElementIf(NodeCharacter(upBufferGraph_->NodesBegin()), upBufferGraph_->NodesEnd(),
                                                [](auto & it) { return !it->Ignore; }, nodecmp);
        stack.push(lowestDegree.get());
        lowestDegree->Ignore = true;
        gMsgUI.Verbose("Removing: %d (tasks=%d, neighbours=%ld, penalty=%ld, size=%ld)", lowestDegree->GetID()-1,
            lowestDegree->AccessTaskCount, lowestDegree.Neighbours, lowestDegree.Penalty, lowestDegree->pBuffer->Size);
    }

    //bank representation structure
    struct GroupCharacteristics
    {
        long Penalty;
        long Reward;
    };
    struct BankCharacteristics
    {
        long Penalty;
        long GroupPenalty;
        long Reward;
        int Capacity = InitialBankCapacity;
        int FreeSpace = InitialBankCapacity;
        int ID = -1;
        GroupCharacteristics * pGroup = nullptr;
    };
    std::vector<BankCharacteristics> banks(nbanks);
    banks[0].Capacity = banks[0].FreeSpace = 5 * 1024;

    constexpr int ngroups = 2;
    std::vector<GroupCharacteristics> groups(ngroups);
    
    for(int i = 0; i < nbanks; ++i)
    {
        banks[i].ID = i;
        banks[i].pGroup = &groups[i%ngroups];
    }
    
    
    //2nd step: Rebuild the graph step by step and color the nodes
    while(!stack.empty())
    {
        auto pn = stack.top();
        stack.pop();
        pn->Ignore = false;
        
        //sum up the penalties and rewards from connected edges for each bank
        for(auto & bank : banks) bank.Penalty = bank.GroupPenalty = bank.Reward = 0;

        for(auto & e : pn->Edges())
        {
            auto pn2 = (e.GetSource() == pn) ? e.GetTarget() : e.GetSource();
            if(pn2->Ignore) continue;
            auto bankidx = pn2->pBuffer->MemBank;
            if(bankidx < 0) continue;
            auto & bank = banks[bankidx];
            
            bank.Penalty += e.Penalty;
            bank.GroupPenalty += e.GroupPenalty;
            bank.Reward += e.Reward;
        }
        
        //sum up bank rewards for each group (on MPPA, groups are right and left)
        for(auto & group : groups) group.Reward = group.Penalty = 0;
        for(auto & bank : banks)
        {
            bank.pGroup->Reward += bank.Reward;
            bank.pGroup->Penalty += bank.GroupPenalty;
        }
        
        //now find optimal bank
        auto bankcmp = Ladybirds::gen::MultiCritCmp(
            [](const BankCharacteristics & b1, const BankCharacteristics & b2)
                {return (-b1.Penalty+b2.Penalty) + (-b1.pGroup->Penalty+b2.pGroup->Penalty);},
            [](const BankCharacteristics & b1, const BankCharacteristics & b2)
                {return b1.Reward-b2.Reward + (b1.pGroup->Reward-b2.pGroup->Reward)*3/8;},
            [](const BankCharacteristics & b1, const BankCharacteristics & b2) {return b1.FreeSpace - b2.FreeSpace;});


        auto buffersize = pn->pBuffer->Size;
        auto it = gen::MaxElementIf(banks.rbegin(), banks.rend(), 
                                    [buffersize](auto & b){ return b.FreeSpace >= buffersize; }, bankcmp);
        if(it != banks.rend())
        {
            pn->pBuffer->MemBank = it->ID;
            it->FreeSpace -= buffersize;
            gMsgUI.Verbose("Assignment: %d -> %d with penalty %ld, reward %ld, group reward %ld. Remaining capacity: %d",
                            pn->GetID()-1, it->ID, it->Penalty, it->Reward, it->pGroup->Reward, it->FreeSpace);
        }
        else
        {
            pn->pBuffer->MemBank = -1;
            gMsgUI.Verbose("Failed to assign bank to buffer %d", pn->GetID()-1);
            ret = false;
        }
    }
    
    if(!ret)
    {
        if(correction < 10)
        {
            gMsgUI.Verbose("Assignment failed. Starting over with correction factor %d", correction+1);
            return AssignBanks(correction+1);
        }
        
        //If we still failed with a very high correction factor, stop trying and display error information
        gMsgUI.Error("Not all buffers could be mapped to memory banks. Printing final assignment status:");
        PrintAssignmentInfo(std::cerr);
    }
    
    gMsgUI.Verbose("Bank usage:");
    for(auto & bank : banks)
    {
        gMsgUI.Verbose("\t%d: %d/%d", bank.ID, bank.Capacity - bank.FreeSpace, bank.Capacity);
    }
    
    return ret;
}

void BankAssignment::PrintAssignmentInfo(std::ostream & strm)
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
}


}} //namespace Ladybirds::opt
