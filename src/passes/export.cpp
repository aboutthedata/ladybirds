// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "lua/luadump.h"
#include "lua/luaenv.h"
#include "lua/pass.h"
#include "program.h"

using Ladybirds::impl::Program;
using Ladybirds::lua::Pass;

/// Export pass: Stores the information contained in a Program object into lua tables for code generation
class ExportPass : public Pass
{
public:
    using Pass::Pass;
    
    virtual int Run(lua_State * lua) override;
};

ExportPass MyExportPass("Export", nullptr);


int ExportPass::Run(lua_State * lua)
{
    auto & prog = GetProgram(lua);
    CheckDependencies(lua, prog);
    
    Ladybirds::lua::LuaDump ld(lua);
    
    //export the program to lua
    if(!ld.RawIO(prog)) return 0;
    
    //add kernels member to output table
    lua_pushliteral(lua, "kernels");
    lua_createtable(lua, 0, prog.Kernels.size());
    for(auto & kpair : prog.Kernels)
    {
        ld.IORef(kpair.first.c_str(), kpair.second);
    }
    lua_rawset(lua, -3);
    
    return 1;
}
