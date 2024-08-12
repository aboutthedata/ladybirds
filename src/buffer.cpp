// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "buffer.h"

#include "kernel.h"
#include "packet.h"

namespace Ladybirds { namespace impl {

bool Buffer::LoadStoreMembers(loadstore::LoadStore &ls)
{
    bool isexternal = false;
    int externalargindex = -1;
    if(pExternalSource)
    {
        isexternal = true;
        auto & arglist = pExternalSource->GetKernel()->Packets;
        externalargindex = pExternalSource - arglist.data();
        assert(((size_t) externalargindex) < arglist.size() && &arglist[externalargindex] == pExternalSource);
    }
    return ls.IO("size", Size) & ls.IO("membank", MemBank) & ls.IO("bankaddress", BankOffset)
         & ls.IO("isexternal", isexternal, false) & ls.IO("extargindex", externalargindex);
}

}} //namespace Ladybirds::impl
