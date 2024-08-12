// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <string>

#include "loadstore.h"
#include "lua/luaenv.h"
#include "lua/luaload.h"
#include "lua/pass.h"
#include "program.h"

namespace {

struct ProjectInfoArgs : public Ladybirds::loadstore::LoadStorableCompound
{
    std::string Filename;
    virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override {return ls.IO("filename", Filename); }
};
bool LoadProjectInfo(Ladybirds::impl::Program &prog, ProjectInfoArgs & args);

/// Pass LoadProjectInfo: Loads all the "project information", i.e. the information relevant for code generation
/** (i.e. which auxiliary files need to be copied, which code files need to be copied and added to the Makefile, ...)**/
Ladybirds::lua::PassWithArgs<ProjectInfoArgs> LoadProjectInfoPass("LoadProjectInfo", &LoadProjectInfo);


bool LoadProjectInfo(Ladybirds::impl::Program &prog, ProjectInfoArgs & args)
{
    Ladybirds::lua::LuaEnv lua;
    if(!lua.DoFile(args.Filename.c_str())) return false;
    
    Ladybirds::lua::LuaLoad ld(lua);
    lua_pushglobaltable(lua);
    
    return ld.IO("auxfiles", prog.AuxFiles, false)
         & ld.IO("codefiles", prog.CodeFiles, false);
}

} //namespace ::
