// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "platform.h"

#include <numeric>
#include <string>

#include "graph/graph-extra.h"

namespace Ladybirds {
namespace spec {

bool Platform::Group::LoadStoreMembers(loadstore::LoadStore &ls)
{
    bool ret = ls.IOHandles("cores", Cores_, ls.UserContext) & ls.IOHandles("mems", Memories_, ls.UserContext);
    if(ret) TotalMem_ = std::accumulate(Memories_.begin(), Memories_.end(), long(0),
                                        [](long l, const Memory *pmem) { return l+pmem->Size; });
    return ret;
}
    
bool Platform::CoreType::LoadStoreMembers(loadstore::LoadStore &ls)
{
    return ls.IO("name", Name);
}
    
bool Platform::Core::LoadStoreMembers(loadstore::LoadStore &ls)
{
    return ls.IO("name", Name) & ls.IOHandle("type", Type, ls.UserContext);
}
    
bool Platform::DmaController::LoadStoreMembers(loadstore::LoadStore &ls)
{
    return ls.IO("name", Name);
}
    
bool Platform::Memory::LoadStoreMembers(loadstore::LoadStore &ls)
{
    return ls.IO("name", Name) & ls.IO("size", Size, true, 0, 1);
}


const Platform::ConnMap & Platform::GetConnMap() const
{
    if(ConnMapVersion_ != Graph_.GetVersion())
    {
        ConnMap_ = graph::EdgeMatrix(Graph_);
        ConnMapVersion_ = Graph_.GetVersion();
    }
    
    return ConnMap_;
}

}} //namespace Ladybirds::spec
