// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "dependency.h"

#include <iostream>

#include "kernel.h"
#include "task.h"
#include "tools.h"

using std::vector;
namespace Ladybirds { namespace spec {
    
using gen::Range;

bool Dependency::Anchor::LoadStoreMembers(loadstore::LoadStore &ls)
{
    Task * ptask;
    std::string packetname;
    
    if(ls.IsStoring())
    {
        assert(TheIface);
        ptask = TheIface->GetTask();
        packetname = TheIface->GetPacket()->GetName();
    }
    
    if(!( ls.IORef("task", ptask)
        & ls.IO("packet", packetname)
        & ls.IO("index", Index.AsVector(), false)))
        return false;
    
    if(ls.IsStoring()) return true;
    
    TheIface = ptask->GetIfaceByName(packetname);
    if(!TheIface)
    {
        ls.Error("Kernel '%s' does not produce/consume a block called '%s'.",
                    TheIface->GetTask()->GetKernel()->Name.c_str(), packetname.c_str());
        return false;
    }

    const auto & dims = TheIface->GetDimensions();
    if(Index.Dimensions() > (int) dims.size() || 
        !std::equal(Index.begin(), Index.end(), dims.begin(), 
                    [](const Range & access, int dim) {return Range::BeginCount(0, dim).Contains(access);}))
    {
        ls.Error("Index out of bounds: Cannot access subarray %s%s of packet %s",
            packetname.c_str(), IndexString(Index).c_str(), TheIface->GetPacket()->GetFullDeclaration().c_str());
        return false;
    }
    
    if(Index.Dimensions() < (int) dims.size())
    {
        Index.reserve(dims.size());
        for(size_t i = Index.Dimensions(); i < dims.size(); ++i)
        {
            Index.push_back(Range::BeginCount(0, dims[i]));
        }
    }
    
    return true;
}


std::string Dependency::Anchor::GetFullId() const
{
    return TheIface->GetTask()->GetFullName() + "." +  TheIface->GetName() + IndexString(Index);
}

int Dependency::Anchor::CalcByteOffset() const
{
    const auto & arraydims = TheIface->GetDimensions();
    
    int basesize = TheIface->GetPacket()->GetBaseType().Size;
    int blocksize = basesize * Product(arraydims.begin()+Index.Dimensions(), arraydims.end());
    int blocknumber = FlattenIndex(Index.begin(), Index.end(), arraydims.begin());
    assert(blocksize >= 1);
    return blocksize*blocknumber;
}



bool Dependency::LoadStoreMembers(loadstore::LoadStore& ls)
{
    if(!( ls.IO("from", From)
        & ls.IO("to", To)))
        return false;
    
    if(!CheckCompatibility())
    {
        const Packet::ArrayDimVector &fromdims = From.TheIface->GetDimensions(), &todims = To.TheIface->GetDimensions();
        
        ls.Error("Cannot connect %s to %s: Types are not compatible (%s%s and %s%s).",
                 From.GetFullId().c_str(), To.GetFullId().c_str(),
                 From.TheIface->GetPacket()->GetBaseType().Name.c_str(),
                 IndexString(fromdims.begin()+From.Index.Dimensions(), fromdims.end()).c_str(),
                 To.TheIface->GetPacket()->GetBaseType().Name.c_str(),
                 IndexString(todims.begin()+To.Index.Dimensions(), todims.end()).c_str());
        return false;
    }

    return true;
}


bool Dependency::CheckCompatibility() const
{
    const Packet &frompack = *From.TheIface->GetPacket(), &topack = *To.TheIface->GetPacket();

    return frompack.GetBaseType().IsCompatible(topack.GetBaseType())
        && (From.Index.GetEffectiveDimensions() == To.Index.GetEffectiveDimensions());
}

long Dependency::GetMemSize() const
{
    return From.Index.GetVolume() * From.TheIface->GetPacket()->GetBaseType().Size;
}

std::ostream &operator<<(std::ostream &strm, Dependency::Anchor &anch)
{
    return strm << anch.TheIface->GetTask()->Name << '.' << anch.TheIface->GetName() << anch.Index;
}


std::ostream &operator<<(std::ostream &strm, Dependency &dep)
{
    return strm << "From " << dep.From << " to " << dep.To;
}


}} //namespace Ladybirds::spec
