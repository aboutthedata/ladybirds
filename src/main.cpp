// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "lua/luaenv.h"
#include "lua/pass.h"
#include "parse/cinterface.h"
#include "cmdlineoptions.h"
#include "msgui.h"
#include "program.h"


int main(int argc, char **argv)
{
    using Ladybirds::tools::gCmdLineOptions;
    gCmdLineOptions.Initialize(argc, argv);
    if(gCmdLineOptions.Verbose) gMsgUI.open(stderr, stdout);
    
    if(gCmdLineOptions.Backend.empty())
    { //no backend which knows what to do; as a fallback, we just parse (and translate) the lb file
        Ladybirds::impl::Program prog;
        Ladybirds::parse::CSpecOptions opt(gCmdLineOptions.ProgramSpec);
        opt.Instrumentation = gCmdLineOptions.Instrumentation;
        return Ladybirds::parse::LoadCSpec(opt, prog) ? 0 : 1;
    }
    
    Ladybirds::lua::LuaEnv lua;
    if(!Ladybirds::lua::RegisterPasses(lua)) return 1;

    bool success = lua.DoFile((gResourceDir + "share/ladybirds/codegen/common/init.lua").c_str(),
                              "Code generator initialisation failed:")
        && lua.DoFile((gCmdLineOptions.Backend + "/main.lua").c_str(), "Error in the backend:");
    
    return success ? 0 : 1;
}

