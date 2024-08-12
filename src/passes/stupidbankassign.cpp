// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <vector>

#include "lua/pass.h"
#include "msgui.h"
#include "kernel.h"
#include "program.h"
#include "task.h"
#include "taskgroup.h"
#include "tools.h"
#include "buffer.h"

using Ladybirds::impl::Buffer;
using Ladybirds::lua::Pass;

namespace {

bool StupidBankAssign(Ladybirds::impl::Program& prog);
Pass StupidBankAssignPass("StupidBankAssign", &StupidBankAssign, Pass::Requires{"BufferPreallocation"});

bool AssignBanks(Ladybirds::impl::Program& prog, Ladybirds::impl::TaskDivision &div)
{
    bool ret = true;
    
    //get a vector "buffers" with all buffers, sorted by descending buffer size
    std::vector<Buffer *> buffers(div.Buffers.size());
    std::transform(div.Buffers.begin(), div.Buffers.end(), buffers.begin(), [](auto & t){return &t;});
    std::sort(buffers.begin(), buffers.end(), [](Buffer * t1, Buffer * t2){ return t1->Size > t2->Size; });

    //get a vector with the banks
    std::vector<int> banks(16, 116*1024);
    std::vector<int> bankoffsets(16, 0);
    banks[0] = 32*1024;
    
    for(Buffer * pt : buffers)
    {
        auto itbank = std::max_element(banks.begin(), banks.end());
        int bank = itbank - banks.begin();
        pt->MemBank = bank;
        pt->BankOffset = bankoffsets[bank];
        bankoffsets[bank] += pt->Size;
        *itbank -= pt->Size;
        if(*itbank < 0)
        {
            gMsgUI.Error("No more space for buffer of size %d", pt->Size);
            ret = false;
        }
    }
    return ret;
}

//! Evaluates dependencies between the tasks and sorts the task list topologically.
//! While doing so, it also sets the reference numbers of the tasks according to their order in the task list.
//! If there are cyclic dependencies, an error message is printed and false is returned.
bool StupidBankAssign(Ladybirds::impl::Program& prog)
{
    bool ret = true;
    
    for(auto & div : prog.Divisions)
    {
        ret = AssignBanks(prog, div) && ret;
    }
    
    return ret;
}

} // namespace ::
