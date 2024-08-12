// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <algorithm>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <numeric>
#include <memory>
#include <iostream>
#include <sstream>

#include "graph/graph.h"
#include "lua/pass.h"
#include "basetype.h"
#include "msgui.h"
#include "kernel.h"
#include "program.h"
#include "range.h"
#include "spacedivision.h"
#include "task.h"
#include "taskgroup.h"
#include "tools.h"

using namespace Ladybirds::spec;
using std::string;
using std::vector;
using Ladybirds::gen::Range;
using Ladybirds::gen::Space;
using Ladybirds::gen::SpaceDivision;
using Ladybirds::impl::Program;
using Ladybirds::impl::Buffer;
using Ladybirds::lua::Pass;

namespace {

bool BufferPreallocation(Program &prog);
Pass BufferPreallocationPass("BufferPreallocation", &BufferPreallocation, Pass::Requires{"CalcSuccessorMatrix"});

    
    
    
class IfaceNode;
class IfaceDependency;
using IfaceGraph = Ladybirds::graph::Graph<IfaceNode>;

class IfaceNode : public Ladybirds::graph::Node<IfaceGraph, IfaceDependency>
{
public:
    Ladybirds::spec::Iface * pIface;
    Buffer * pBuffer = nullptr;
    
public:
    IfaceNode(Ladybirds::spec::Iface * iface) : pIface(iface) {}
};

class IfaceDependency : public Ladybirds::graph::Edge<IfaceNode>
{
public:
    Ladybirds::spec::Dependency * pDependency;
    
public:
    IfaceDependency(Ladybirds::spec::Dependency * dep) : pDependency(dep) {}
};


/**\internal
 * From the program \p prog, constructs a interface graph \p g in which all interfaces are represented by nodes and all
 * dependencies are represented by edges. **/
bool CreateIfaceGraph(Program & prog, IfaceGraph & g)
{
    bool ret = true;
    
    assert(g.IsEmpty());
    std::unordered_map<Ladybirds::spec::Iface*, IfaceNode*> nodemap;
    
    auto addifaces = [&](Task & t)
    {
        for(auto & d : t.Ifaces)
        {
            auto * n = g.EmplaceNode(&d);
            nodemap[&d] = n;
        }
    };
    addifaces(prog.MainTask);
    for(auto & t : prog.GetTasks()) addifaces(t);
    
    for(auto & dep : prog.Dependencies)
    {
        Iface *psource = dep.From.TheIface, *ptarget = dep.To.TheIface;
        
        if(ptarget->GetPacket()->GetAccessType() == Packet::out && ptarget->GetTask() != &prog.MainTask)
        {
            gMsgUI.Error("Trying to write to output iface %s", dep.To.GetFullId().c_str());
            ret = false;
            continue;
        }

        if(!dep.CheckCompatibility())
        {
            gMsgUI.Error("Internal error: dependency between incompatible ifaces: ") << " * " << dep << std::endl;
            ret = false;
            continue;
        }
        
        g.EmplaceEdge(nodemap.at(psource), nodemap.at(ptarget), &dep);
    }
    
    return ret;
}

inline string IfaceId(const Iface * piface)
{
    return piface->GetTask()->GetFullName() + '.' + piface->GetName();
}

/// \internal Checks if each input datum for \p n is defined once and once only
bool CheckIfaceInput(const IfaceNode & n)
{
    bool ret = true;
    
    SpaceDivision<const Dependency*> sd(Space(n.pIface->GetDimensions()));
    sd.AssignSection(sd.GetFullSpace(), nullptr); //assign dummy so we can check later if everything is connected
    
    for(auto & e : n.InEdges())
    {
        const Dependency * pdep = e.pDependency;
        for(auto it : sd.FindOverlaps(pdep->To.Index))
        {
            if(it->first != nullptr) //Overlap with dummy is OK
            {
                std::stringstream ss;
                ss << "Overlapping accesses for " << IfaceId(n.pIface);
                ss << ": From " << IfaceId(it->first->From.TheIface);
                ss << " and " << IfaceId(pdep->From.TheIface);
                ss << " on index/indices " << (it->first->To.Index & pdep->To.Index);
                gMsgUI.Error(ss.str().c_str());
                ret = false;
            }
        }
        
        sd.AssignSection(pdep->To.Index, pdep);
    }
    
    auto dummies = sd.GetSections().equal_range(nullptr);
    if(dummies.first == dummies.second) return ret; //dummy section has been completely eradicated, all inputs are set
    
    if(dummies.first->second == sd.GetFullSpace()) //complete dummy section still there: no input at all
    {
        gMsgUI.Error("Unconnected input iface: %s", IfaceId(n.pIface).c_str());
    }
    else
    {
        for(; dummies.first != dummies.second; ++dummies.first)
        {
            std::stringstream ss;
            ss << "Unconnected input interface: " << IfaceId(n.pIface) << " on index/indices " << dummies.first->second;
            gMsgUI.Error(ss.str().c_str());
        }
    }
    return false;
}

/** \internal
 * Checks that, if the same data goes to multiple readers, no one of them modifies it.
 * (This would be the typical produce–read–modify scenario, which should already have been resolved by this point.)
 **/
bool CheckIfaceOutput(const IfaceNode & n, Task * mainentry, const Program::ReachabilityMap & reachmap)
{
    bool ret = true;
    
    for(auto & e1 : n.OutEdges())
    {
        Dependency * pdep1 = e1.pDependency;
        if(pdep1->To.TheIface->GetPacket()->GetAccessType() == Packet::in) continue; //mere inputs can happen in parallel
        
        bool newerror = true;
        for(auto & e2 : n.OutEdges())
        {
            Dependency * pdep2 = e2.pDependency;
            if(pdep1 == pdep2) continue;
            
            if(pdep1->From.Index.Overlaps(pdep2->From.Index))
            {
                //if the first task (writing to the packet) depends (possibly indirectly) on the second one,
                //and that one only reads, then there is no problem
                Task *pwriter = pdep1->To.TheIface->GetTask(), *preader = pdep2->To.TheIface->GetTask();
                if(pdep2->To.TheIface->GetPacket()->GetAccessType() == Packet::in
                   && (pwriter == mainentry || reachmap[preader].Contains(pwriter))) continue; 
                
                if(newerror)
                {
                    gMsgUI.Error("Unresolved false dependency:") << " * " << *pdep1 << std::endl;
                    newerror = false;
                }
                gMsgUI.Error() << " * " << *pdep2 << std::endl;;
                
                ret = false;
            }
        }
    }
    return ret;
}


/**\internal
 * Checks if the write accesses for each input/inout interface don't overlap.
 * Interfaces belonging to \p mainentry are excluded from the check.
 **/
bool CheckAccesses(IfaceGraph & dg, Task * mainentry, const Program::ReachabilityMap & reachmap)
{
    bool ret = true;
    for(auto & n : dg.Nodes())
    {
        const Iface* piface = n.pIface;
        if(piface->GetTask() == mainentry) continue;
        
        if(piface->GetPacket()->GetAccessType() != Packet::out) ret = CheckIfaceInput(n) && ret;
        if(piface->GetPacket()->GetAccessType() != Packet::in)  ret = CheckIfaceOutput(n, mainentry, reachmap) && ret;
    }
    return ret;
}

/** \internal
 * Adds the "gang" of \p node, i.e. all the interfaces directly or indirectly connected with it through edges, to \p gang.
 * All these interfaces will use the same buffer, and the pBuffer member of the nodes is set to \p pBuffer.
 **/
void GetBufferGang(IfaceNode & node, Buffer * pBuffer, vector<Iface*> & gang)
{
    gang.push_back(node.pIface);
    node.pBuffer = pBuffer;
    
    for(auto & edge : node.OutEdges())
    {
        auto * pn = edge.GetTarget();
        if(!pn->pBuffer) GetBufferGang(*pn, pBuffer, gang);
    }
    for(auto & edge : node.InEdges())
    {
        auto * pn = edge.GetSource();
        if(!pn->pBuffer) GetBufferGang(*pn, pBuffer, gang);
    }
}

//! \internal Returns the space containing the PosHint indices of all interfaces in \p gang (so to say the union of PosHints)
Space GetIndexSpace(vector<Iface*> & gang)
{
    auto it = gang.begin(), itend = gang.end();
    Space s = (*it)->PosHint;
    while(++it != itend)
        s |= (*it)->PosHint;
    return s;
}

/** \internal Determines the required size of \p pbuffer such that it can hold all interfaces.
 * Then, determines the position of each interface inside the buffer and the according displacement vectors
 * (to be passed on to the actual tasks later during execution). Finally, sets up the interfaces accordingly. **/
void AdjustIndices(vector<Iface*> & gang, Buffer * pbuffer)
{
    auto s = GetIndexSpace(gang);
    auto origin = s.GetOrigin();
    s.DisplaceNeg(origin);
    auto spbufferdim = std::make_shared<std::vector<int>>();
    auto dim = *spbufferdim = s.GetDimensions();
    
    //Calculate multiplication vector for all dimensions
    std::vector<int> mulvec(dim.size());
    int mul = 1;
    for(auto i = mulvec.size(); i-- > 0; )
    {
        mulvec[i] = mul;
        mul *= dim[i];
    }
    int elemsizeof = gang[0]->GetPacket()->GetBaseType().Size;
    pbuffer->Size = mul*elemsizeof;
    
    //Calculate offset and displacement vectors for each interface, and set up the interface accordingly
    for(Iface * pd : gang)
    {
        auto & ph = pd->PosHint;
        ph.DisplaceNeg(origin);
        auto offset = ph.GetOrigin();
        
        std::vector<int> dispvec = pd->GetDimensions();
        auto itidx = ph.rbegin();
        auto itdim = dim.rbegin();
        int mul = 1;
        for(auto itdisp = dispvec.rbegin(), itdispend = dispvec.rend(); itdisp != itdispend; ++itdisp)
        {
            while(*itdisp != itidx->size())
            {
                assert(itidx->size() == 1);
                ++itidx, mul *= *(itdim++);
            }
            *itdisp = mul;
            ++itidx, mul = *(itdim++);
        }
        pd->SetBuffer(pbuffer, spbufferdim, std::move(dispvec),
                       std::inner_product(offset.begin(), offset.end(), mulvec.begin(), 0)*elemsizeof);
    }
}


/** Determines what buffers are necessary and calculates for each interface which buffer it accesses at which indices **/
bool BufferPreallocation(Program &prog)
{
    for(Iface & d : prog.MainTask.Ifaces) d.PosHint = Space(d.GetDimensions());
    
    IfaceGraph dg;
    if(!CreateIfaceGraph(prog, dg)) return false;
    if(!CheckAccesses(dg, &prog.MainTask, prog.TaskReachability)) return false;
    
    bool ret = true;
    for(auto & n : dg.Nodes())
    {
        Iface * piface = n.pIface;
        if(piface->GetBuffer()) continue; //interface already handled
        
        auto * ptask = piface->GetTask();
        Buffer *pbuffer;
        if(ptask == &prog.MainTask)
        {
            pbuffer = &*prog.ExternalBuffers.emplace();
            pbuffer->pExternalSource = piface->GetPacket();
        }
        else
        {
            auto * pgrp = ptask->Group; assert(pgrp);
            auto * pdiv = pgrp->GetDivision(); assert(pdiv);
            pbuffer = &*pdiv->Buffers.emplace();
        }
        
        vector<Iface*> gang;
        GetBufferGang(n, pbuffer, gang);
        AdjustIndices(gang, pbuffer);
    }
    return ret;
}

} //namespace ::
