// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "metakernelseq.h"

#include <algorithm>
#include <array>
#include <forward_list>
#include <memory>
#include <numeric>
#include <unordered_map>

#include "kernel.h"
#include "packet.h"
#include "range.h"
#include "spacedivision.h"
#include "task.h"
#include "tools.h"

namespace Ladybirds { namespace parse {

using Ladybirds::gen::Space;
using Ladybirds::gen::SpaceDivision;
using Ladybirds::spec::Kernel;
using Ladybirds::spec::Packet;
using Ladybirds::spec::Task;
using Ladybirds::spec::Iface;

MetaKernelSeq::KernelCall::Argument::Argument(const Packet *variable, RangeVec indices)
    : Variable_(variable), Indices_(std::move(indices))
{
    auto & vdims = Variable_->GetArrayDims();
    
    size_t oldsize = Indices_.Dimensions();
    if(oldsize > vdims.size())
    {
        ErrorDesc_ = "Too many indexing operations.\n";
        Indices_.AsVector().resize(vdims.size());
    }
    else if(oldsize < vdims.size())
    {
        Indices_.reserve(vdims.size());
        for(auto i = oldsize, imax = vdims.size(); i < imax; ++i)
            Indices_.push_back(Range::BeginCount(0, vdims[i]));
    }
    
    for(int i = 0, imax = Indices_.Dimensions(); i < imax; ++i)
    {
        Range & r = Indices_[i];

        if(!Range::BeginCount(0, vdims[i]).Contains(r))
        {
            ErrorDesc_ += strprintf("Out of bounds access for index %d of variable %s\n", i+1, 
                                    Variable_->GetName().c_str());
        }
        
        switch(r.size())
        {
            case 0:
                ErrorDesc_ += strprintf("Zero-sized range for index %d of variable %s\n", i+1, 
                                        Variable_->GetName().c_str());
                break;
            case 1:
                break;
            default:
                ResultingDim_.push_back(r.size());
        }
    }
    
    if(!ErrorDesc_.empty()) ErrorDesc_.pop_back(); //remove last '\n'
}



MetaKernelSeq::KernelCall::KernelCall(Kernel * callee, ArgVec args, ParamVec params, ParamVec derivedparams)
: Callee_(callee), Args_(std::move(args)), Params_(std::move(params)), DerivedParams_(std::move(derivedparams))
{
    auto & packets = Callee_->Packets;
    if(Args_.size() != packets.size())
    {
        ErrorDesc_ = "Wrong number of arguments.";
        Valid_ = false;
        return;
    }
    
    for(int argidx = 0, imax = Args_.size(); argidx < imax; ++argidx)
    {
        auto & arg = Args_[argidx];
        const auto &supply = *arg.GetVariable(), &demand = packets[argidx];
        
        if(!supply.GetBaseType().IsCompatible(demand.GetBaseType()))
        {
            ErrorDesc_ += strprintf("Incompatible base type for argument %d: Passed %s where %s was requested\n",
                                    argidx+1, supply.GetBaseType().Name.c_str(), demand.GetBaseType().Name.c_str());
        }
        
        if(supply.GetAccessType() == Packet::in && demand.GetAccessType() != Packet::in)
        {
            ErrorDesc_ += strprintf("Incompatible access type for argument %d: Passed %s where %s was requested\n",
                                    argidx+1, Packet::AccessTypeNames[supply.GetAccessType()],
                                    Packet::AccessTypeNames[demand.GetAccessType()]);
        }
        
         // Collapse the array indices, also according to what the called function demands
        Argument::ArrayDimVec argdims = demand.GetArrayDims();
        for(int & dim : argdims) if (dim < 0) dim = DerivedParams_[-dim-1];
        
        auto & indices = arg.GetIndices();
        arg.RelevantDims_.resize(argdims.size());
        int suppidx = indices.Dimensions();
        
        for(int i = argdims.size(); i-- > 0; )
        {
            int curargdim = argdims[i];
            if(curargdim < 0) curargdim = DerivedParams_[-curargdim-1];
            int cursize = suppidx ? indices[--suppidx].size() : -1;
            if(cursize != curargdim)
            {
                while(cursize == 1 && suppidx > 0) cursize = indices[--suppidx].size();
                
                if(cursize != curargdim)
                {
                    ErrorDesc_ += strprintf("Incompatible block size for argument %d: Passed %s where %s was requested\n",
                                            Params_.size()+argidx+1, IndexString(arg.GetResultingDim()).c_str(), 
                                            IndexString(argdims).c_str());
                    suppidx = 0;
                    break;
                }
            }
                
            arg.RelevantDims_[i] = suppidx;
        }
        if(std::any_of(indices.begin(), indices.begin()+suppidx, [](auto & r){return r.size() != 1;}))
        {
            ErrorDesc_ += strprintf("Incompatible block size for argument %d: Passed %s where %s was requested\n",
                                    Params_.size()+argidx+1, IndexString(arg.GetResultingDim()).c_str(), 
                                    IndexString(argdims).c_str());
            break;
        }
    }
    
    if(ErrorDesc_.empty())
    {
        Valid_ = std::all_of(Args_.begin(), Args_.end(), [](Argument & arg){return arg.IsValid();});
    }
    else
    {
        Valid_ = false;
        ErrorDesc_.pop_back(); //remove last '\n'
    }
}

void Dump(MetaKernelSeq::KernelCall & call)
{
    std::cout << call.GetCallee()->Name << "( ";
    const char * sep = "";
    for(auto & arg : call.GetArguments())
    {
        std::cout << sep << arg.GetVariable()->GetName() << '[' << arg.GetIndices() << ']';
        sep = ", ";
    }
    std::cout << ");" << std::endl;
}
    
    
    
MetaKernelSeq::MetaKernelSeq(spec::MetaKernel * pmetakernel)
  : pMetaKernel(pmetakernel)
{
}


namespace {

Space IndicesAbsToRel(const Space & abs, const Space & ref, const std::vector<int> & relvdims)
{
    Space rel; rel.reserve(relvdims.size());
    for(int dim : relvdims)
    {
        rel.push_back(abs[dim] - ref[dim].first());
    }
    return rel;
}

} // namespace ::


bool MetaKernelSeq::TranslateToMetaKernel(std::string& errors)
{
    auto & mk = *pMetaKernel;
    
    if(!std::all_of(Operations.begin(), Operations.end(), [](auto & op){return op.IsValid();})) return false;
    
    //at each operation, this map will hold for each variable a list of currently live definitions
    //(the Argument * holds the output packet that wrote to this section)
    std::unordered_map<const Packet*, SpaceDivision<const KernelCall::Argument *>> defs;
    std::forward_list<KernelCall::Argument> mkargs; //dummies for the input/inout packets of this kernel
    
    //for each kernel, this map holds the instantiation count such that each task gets a unique index
    std::unordered_map<const Kernel*, int> instcounts;
    
    
    //initialize the list with either null (out packets and temporary variables) or input task (in and inout packets)
    for(auto & uppacket : Variables)
    {
        auto & sdiv = defs.emplace(&uppacket, Space(uppacket.GetArrayDims())).first->second;
        sdiv.AssignSection(sdiv.GetFullSpace(), nullptr);
    }
    for(auto i = mk.Packets.size(); i-- > 0; )
    {
        auto & packet = mk.Packets[i];
        auto & sdiv = defs.emplace(&packet, Space(packet.GetArrayDims())).first->second;
        if(packet.GetAccessType() == Packet::out)
        {
            sdiv.AssignSection(sdiv.GetFullSpace(), nullptr);
        }
        else
        {
            mkargs.emplace_front(&packet, Space(packet.GetArrayDims()));
            auto & arg = mkargs.front();
            assert(arg.IsValid());
            arg.RelevantDims_.assign(packet.GetArrayDims().size(), int());
            std::iota(arg.RelevantDims_.begin(), arg.RelevantDims_.end(), 0);
            arg.Iface_ = &mk.Inputs->Ifaces[i];
            sdiv.AssignSection(sdiv.GetFullSpace(), &arg);
        }
    }
    
    //now add a task for every operation and add a dependency for each live definition
    for(KernelCall & op : Operations)
    {
        auto * kernel = op.GetCallee();
        auto uptask = std::make_unique<Task>(kernel, strprintf("%s[%d]", kernel->Name.c_str(), instcounts[kernel]++),
                                             op.Params_, op.DerivedParams_);
        
        //assign interfaces to arguments
        for(auto i = op.GetArguments().size(); i-- > 0; )
        {
            KernelCall::Argument & arg = op.Args_[i];
            arg.Iface_ = &uptask->Ifaces[i];
            arg.Iface_->PosHint = arg.GetIndices();
            arg.Iface_->BufferHint = arg.BufferHint_;
        }
        
        // Now determine the dependencies between the interfaces. To do this, we first have to handle the inputs,
        // then the outputs. Otherwise, we might get self-dependencies.
        // This loop handles the inputs...
        for(auto & arg : op.GetArguments())
        {
            if(arg.Iface_->GetPacket()->GetAccessType() == Packet::out) continue;
            
            const Packet & var = *arg.GetVariable();
            auto & vardefs = defs.at(&var);
            
            auto subdiv = vardefs.SubDivision(arg.GetIndices());
            for(auto secpair : subdiv.GetSections())
            {
                auto *pdef = secpair.first;
                const Space & defrange = secpair.second;
                if(pdef == nullptr)
                {
                    errors += strprintf("Kernel call %s: Use of uninitialized variable %s as input "
                        "(uninitialized in indices %s)\n", uptask->GetFullName().c_str(), 
                        var.GetName().c_str(), DumpToString(defrange).c_str());
                }
                else
                {
                    using Anchor = spec::Dependency::Anchor;
                    mk.Dependencies.emplace_back(
                        Anchor(pdef->Iface_, IndicesAbsToRel(defrange, pdef->GetIndices(), pdef->GetRelevantDims())),
                        Anchor(arg.Iface_, IndicesAbsToRel(defrange, arg.GetIndices(), arg.GetRelevantDims())));
                }
            }
        }
        
        //...and this loop the outputs.
        for(auto & arg : op.GetArguments())
        {
            if(arg.Iface_->GetPacket()->GetAccessType() == Packet::in) continue;

            auto & vardefs = defs.at(arg.GetVariable());
            vardefs.AssignSection(arg.GetIndices(), &arg);
        }
        mk.Tasks.push_back(std::move(uptask));
    }
    
    // Finally check if all output packets have been completely written
    for(size_t i = 0, imax = mk.Packets.size(); i < imax; ++i)
    {
        auto & packet = mk.Packets[i];
        if(packet.GetAccessType() == Packet::in) continue;
        
        for(auto & secpair : defs.at(&packet).GetSections())
        {
            auto *pdef = secpair.first;
            const Space & defrange = secpair.second;
            
            if(pdef)
            {
                using Anchor = spec::Dependency::Anchor;
                mk.Dependencies.emplace_back(
                    Anchor(pdef->Iface_, IndicesAbsToRel(defrange, pdef->GetIndices(), pdef->GetRelevantDims())),
                    Anchor(&mk.Outputs->Ifaces[i], defrange));
            }
            else if(packet.GetAccessType() == Packet::out)
            {
                errors += strprintf("Kernel output '%s' is unspecified for indices %s\n", 
                                    packet.GetName().c_str(), DumpToString(secpair.second).c_str());
            }
        }
    }
    return errors.empty();
}








}} //namespace Ladybirds::parse
