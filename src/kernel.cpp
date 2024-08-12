// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "kernel.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace Ladybirds { namespace spec {

Kernel::Kernel(const Kernel& other)
 : Name(other.Name), FunctionName(other.FunctionName), CodeFile(other.CodeFile), SourceCode(other.SourceCode),
   Packets(other.Packets)
{
    std::unordered_map<const Packet*, Packet*> p2p;
    for(auto i = Packets.size(); i-- > 0; )
    {
        p2p[&other.Packets[i]] = &Packets[i];
    }
    
    for(Packet & packet : Packets)
    {
        packet.Kernel_ = this;
        Packet::BuddyList newbuddies; newbuddies.reserve(packet.Buddies_.size());
        for(auto buddy : packet.Buddies_) newbuddies.insert(p2p[buddy]);
        packet.Buddies_ = std::move(newbuddies);
    }
}


bool Kernel::LoadStoreMembers(loadstore::LoadStore& ls)
{
    return ls.IO("name", Name)
         & ls.IO("func", FunctionName, false, Name)
         & ls.IO("codefile", CodeFile, false, Name + ".c")
         & ls.IO("source", SourceCode, false)
         & ls.IO_Register("packets", Packets)
         & ls.IO("parameters", Params);
}

Packet* Kernel::PacketByName(const std::string& name)
{
    auto it = std::find_if(Packets.begin(), Packets.end(),
        [&name](auto & packet) {return packet.GetName() == name;});
    if(it == Packets.end()) return nullptr;
    else return &*it;
}

}}//namespace Ladybirds::spec
