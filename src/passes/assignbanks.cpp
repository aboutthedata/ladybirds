// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include "lua/pass.h"
#include "opt/bankassignment.h"
#include "opt/cacheindexopt.h"
#include "spec/platform.h"
#include "msgui.h"
#include "program.h"
#include "task.h"
#include "taskgroup.h"


using std::string;
using std::deque;
using std::unordered_map;
using Ladybirds::spec::Task;
using Ladybirds::lua::Pass;
using Ladybirds::lua::PassWithArgs;

namespace {

struct BankAssignmentArgs : public Ladybirds::loadstore::LoadStorableCompound
{
    std::string TimingSpec;
    virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override
        {return ls.IO("timingspec", TimingSpec); }
};
bool AssignBanks(Ladybirds::impl::Program &prog, BankAssignmentArgs & args);

/// Pass AssignBanks: Based on a measured task start and end times (timingspec), assign all buffers to memory banks
PassWithArgs<BankAssignmentArgs> AssignBanksPass("AssignBanks", &AssignBanks, 
                                                 Pass::Requires{"BufferPreallocation"});


bool AssignBanks(Ladybirds::impl::Program &prog, BankAssignmentArgs & args)
{
    Ladybirds::opt::BankAssignment ba(prog, 16);
    if(!ba.LoadOverlaps(args.TimingSpec.c_str())) return false;
    
    //MPPA data. TODO: make configurable
    Ladybirds::spec::Platform::CacheConfig cacheinfo = {64, 2, 64};
    Ladybirds::spec::Platform::Cluster clusterinfo = {16, 16, 116*1024};
    Ladybirds::opt::CacheIndexOpt cio(clusterinfo, cacheinfo);

    bool ret = true;
    for(auto &div : prog.Divisions) 
    {
        ba.CreateBufferGraph(div);
        if(gMsgUI.IsVerbose()) ba.GenerateBufferGraphFile();
        
        ret = ba.AssignBanks() && cio.Optimize(div) && ret;
    }
    return ret;
}

} //namespace ::
