// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <assert.h>
#include <numeric>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "spacedivision.h"
#include "lua/pass.h"
#include "program.h"
#include "taskgroup.h"
#include "tools.h"


using Ladybirds::gen::Space;
using Ladybirds::impl::Port;
using Ladybirds::impl::TaskGroup;
using Ladybirds::impl::Buffer;
using Ladybirds::impl::Program;
using Ladybirds::spec::Dependency;

namespace {

constexpr bool gDbgOut = false;


struct MergePortsArgs : public Ladybirds::loadstore::LoadStorableCompound
{
    std::vector<int> InLimits;
    std::vector<int> OutLimits;
    virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override
        { return ls.IO("inlimits", InLimits, false) & ls.IO("outlimits", OutLimits, false); }
};
bool MergePorts(Program &prog, MergePortsArgs & args);

/** Pass MergePortsByBuffer: Deletes all channels and merges/prunes the ports such that loading/storing data from/to
 a non-volatile memory can be performed efficiently, i.e. without (at least ideally without) loading/storing data twice
 and without too many small load/store operations.**/
Ladybirds::lua::PassWithArgs<MergePortsArgs> MergePortsPass("MergePortsByBuffer", &MergePorts);

bool IsWithinLimits(const Space &s, const std::vector<int> &limits)
{
    auto it = s.rbegin(), itend = s.rend();
    auto itlimits = limits.begin(), itlimitsend = limits.end();
    
    for(; it != itend && itlimits != itlimitsend; ++it, ++itlimits)
        if(it->size() <= *itlimits) return true;
    
    int limit = limits.back();
    for(; it != itend; ++it) if(it->size() <= limit) return true;
    
    return false;
}

void Invalidate(Port &p)
{
    p.Invalidate();
    //Do not invalidate the whole channel, for we might need the port on the other side
}


//Remove areas that are loaded or stored twice
template<class iterator>
void PrunePortList(iterator itstart, iterator itend)
{
    auto *bufferdims = (*itstart)->BufferDims;
    Ladybirds::gen::SpaceDivision<Port*> sd((Space(*bufferdims)));
    
    for(auto it = itend; it-- != itstart; )
    {
        Port &p = **it;
        if(!p.IsValid()) continue;
        assert(p.BufferDims == bufferdims);
        sd.AssignSection(p.Position, &p);
    }
    
    for(auto it = itstart; it != itend; ++it)
    {
        Port &p = **it;
        if(!p.IsValid()) continue;

        if(gDbgOut) std::cout << "   * " << *p.GetIface() << ": " << p.Position << std::endl;
        
        auto env = sd.GetEnvelope(&p);
        if(env.IsEmpty())
        {
            if(gDbgOut) std::cout << "      + Erasing altogether..." << std::endl;
            Invalidate(p);
        }
        else
        {
            if(gDbgOut && env != p.Position) std::cout << "      + Shrinked to " << env << std::endl;
            p.Position = env;
        }
    }
}

struct PortMergeInfo
{
    Port *Port1, *Port2;
    bool SameIface;
    Space ResultingPos;
    int ResultingSize;
    int Cost;
    
    // compare two different merge options: the "smaller" merge option is more favorable
    bool operator <(const PortMergeInfo &other) const
    {
        if(SameIface != other.SameIface) return SameIface;
        if(Cost != other.Cost) return (Cost < other.Cost);
        return (ResultingSize < other.ResultingSize);
    }
};

template<class iterator>
auto FindMergeOptions(iterator itstart, iterator itend)
{
    std::vector<PortMergeInfo> ret;
    int nports = itend-itstart;
    ret.reserve(nports*(nports-1)/2);
    for(auto it = itstart; it != itend; ++it)
    {
        Port &p1 = **it;
        if(!p1.IsValid()) continue;
        
        for(auto it2 = itstart; it2 < it; ++it2)
        {
            Port &p2 = **it2;
            if(!p2.IsValid()) continue;

            PortMergeInfo info = {&p2, &p1, p1.GetIface() == p2.GetIface(), p1.Position | p2.Position, 0, 0};
            info.ResultingSize = info.ResultingPos.GetVolume();
            info.Cost = info.ResultingSize - p1.Position.GetVolume() - p2.Position.GetVolume();
            ret.push_back(std::move(info));
        }
    }
    return ret;
}

template<class iterator>
void MergePortList(iterator itstart, iterator itend, const std::vector<int> &limits)
{
    const PortMergeInfo *performedmerge;
    do
    {
        performedmerge = nullptr;
        std::unordered_set<const Port*> mergedports;

        auto mergeoptions = FindMergeOptions(itstart, itend);
        std::sort(itstart, itend);
        if(gDbgOut) std::cout << mergeoptions.size() << " merge options" << std::endl;
        for(auto &mergeoption : mergeoptions)
        {
            if(gDbgOut) 
                std::cout << "Option: " << mergeoption.Port1->GetIface()->GetFullName() << mergeoption.Port1->Position
                    << ", " << mergeoption.Port2->GetIface()->GetFullName() << mergeoption.Port2->Position
                    << ". Cost=" << mergeoption.Cost << ", Size=" << mergeoption.ResultingSize << std::endl;
            if(performedmerge && *performedmerge < mergeoption) break;
            
            if(mergedports.count(mergeoption.Port1) || mergedports.count(mergeoption.Port2)) continue;
            if(mergeoption.Cost > 0) continue;
            if(!mergeoption.SameIface && !IsWithinLimits(mergeoption.ResultingPos, limits)) continue;
            
            performedmerge = &mergeoption;
            mergedports.insert(mergeoption.Port1);
            mergedports.insert(mergeoption.Port2);
            mergeoption.Port1->Position = mergeoption.ResultingPos;
            Invalidate(*mergeoption.Port2);
        }
    }
    while(performedmerge);
}


template<class operations>
void MergeByBuffers(TaskGroup &grp, std::vector<int> &limits)
{
    //collect all inputs/outputs referring to same buffer
    std::unordered_map<const Buffer*, std::vector<Port*>> opmap;
    for(auto &upop : grp.GetOperations())
    {
        for(auto &upport : operations::getports(*upop))
        {
            auto &iface = *upport->GetIface();
            auto *chan = upport->GetChannel();
            if(!chan)
            {
                upport->Invalidate();
                continue;
            }
            upport->Position = operations::getanchor(*chan->Dep).Index;
            if(gDbgOut) std::cout << "Index: " << upport->Position << ", PosHint: " << iface.PosHint << std::endl;
            upport->Position.Displace(iface.PosHint.GetOrigin());
            upport->BufferDims = &iface.GetBufferDims();
            upport->BufferBaseTypeSize = iface.GetPacket()->GetBaseType().Size;
            upport->Disconnect();
            opmap[iface.GetBuffer()].push_back(upport.get());
        }
    }
    
    int d1limit = limits[0];
    for(auto &entry : opmap)
    {
        auto &ports = entry.second;
        if(ports.size() > 1)
        {
            limits[0] = d1limit / ports[0]->BufferBaseTypeSize;
            if(gDbgOut) std::cout << "Pruning " << *entry.second[0]->GetIface() << std::endl;
            PrunePortList(operations::mergebegin(ports), operations::mergeend(ports));
            if(gDbgOut) std::cout << "Merging" << std::endl;
            MergePortList(operations::mergebegin(ports), operations::mergeend(ports), limits);
        }
    }
    limits[0] = d1limit;
}



bool MergePorts(Program &prog, MergePortsArgs &args)
{
    struct inops
    {
        static TaskGroup::Operation::PortList & getports(TaskGroup::Operation &op) { return op.Inputs; };
        static Dependency::Anchor & getanchor(Dependency &dep) { return dep.To; };
        static auto mergebegin(std::vector<Port*> &l) { return l.begin(); }
        static auto mergeend(std::vector<Port*> &l) { return l.end(); }
    };
    struct outops
    {
        static TaskGroup::Operation::PortList & getports(TaskGroup::Operation &op) { return op.Outputs; };
        static Dependency::Anchor & getanchor(Dependency &dep) { return dep.From; };
        static auto mergebegin(std::vector<Port*> &l) { return l.rbegin(); }
        static auto mergeend(std::vector<Port*> &l) { return l.rend(); }
    };
    
    if(args.InLimits.empty()) args.InLimits.push_back(0);
    if(args.OutLimits.empty()) args.OutLimits.push_back(0);
    for(auto &div : prog.Divisions)
    {
        for(auto pgrp : div.GetGroups())
        {
            MergeByBuffers<inops>(*pgrp, args.InLimits);
            MergeByBuffers<outops>(*pgrp, args.OutLimits);
            pgrp->PortCleanup();
        }
    }

    prog.Channels.clear();
    
    return true;
}

} //namespace ::
