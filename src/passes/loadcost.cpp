// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <string>
#include <unordered_map>

#include "lua/luaenv.h"
#include "lua/luaload.h"
#include "lua/pass.h"
#include "loadstore.h"
#include "msgui.h"
#include "program.h"
#include "task.h"


using std::string;
using Ladybirds::spec::Task;

struct CostArgs : public Ladybirds::loadstore::LoadStorableCompound
{
    std::string Filename;
    virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override {return ls.IO("filename", Filename); }
};

static bool LoadCost(Ladybirds::impl::Program &prog, CostArgs & args);

Ladybirds::lua::PassWithArgs<CostArgs> LoadCostPass("LoadCost", &LoadCost);

static bool LoadCost(Ladybirds::impl::Program &prog, CostArgs & args)
{
    Ladybirds::lua::LuaEnv lua;
    if(!lua.DoFile(args.Filename.c_str())) return false;
    lua_getglobal(lua, "costs");
    lua_getglobal(lua, "kernelcosts");
    bool havecosts = !lua_isnil(lua, -2), havekernelcosts = !lua_isnil(lua, -1);
    if(!havecosts && !havekernelcosts)
    {
        gMsgUI.Error("Cost specification neither defines 'costs' table nor a 'kernelcosts' table");
        return false;
    }
        
    Ladybirds::lua::LuaLoad load(lua);
    std::unordered_map<std::string, double> costs, kernelcosts;

    lua_pushglobaltable(lua);
    if(havecosts && !load.IO("costs", costs)) return false;
    if(havekernelcosts && !load.IO("kernelcosts", kernelcosts)) return false;
    
    for(auto & t : prog.GetTasks())
    {
        auto it = costs.find(t.Name);
        if(it != costs.end() || (it = kernelcosts.find(t.GetKernel()->Name)) != kernelcosts.end())
            t.Cost = it->second;
        else gMsgUI.Warning("No cost defined for task %s, nor for its kernel %s",
                            t.Name.c_str(), t.GetKernel()->Name.c_str());
    }
    return true;
}
