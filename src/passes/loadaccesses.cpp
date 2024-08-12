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
using Ladybirds::loadstore::LoadStore;

namespace {

struct AccessArgs : public Ladybirds::loadstore::LoadStorableCompound
{
    std::string Filename;
    virtual bool LoadStoreMembers(LoadStore &ls) override {return ls.IO("filename", Filename); }
};

bool LoadAccesses(Ladybirds::impl::Program &prog, AccessArgs & args);

Ladybirds::lua::PassWithArgs<AccessArgs> LoadAccessesPass("LoadAccessCounts", &LoadAccesses);


class TaskAccessesLoader : public Ladybirds::loadstore::LoadStorableCompound
{
private:
    Task &Target;
    
public:
    TaskAccessesLoader(Task &t) : Target(t) {}
    
    bool LoadStoreMembers(LoadStore &ls) override
    {
        std::vector<int> rwcounts; rwcounts.reserve(2);
        bool ret = true;
        for(auto &d : Target.Ifaces)
        {
            rwcounts.clear();
            ret &= ls.IO(d.GetName().c_str(), rwcounts);
            if(rwcounts.size() == 2)
            {
                d.Reads = rwcounts[0], d.Writes = rwcounts[1];
            }
            else
            {
                ls.Error("Invalid access statistics for %s. Expected format: {rcount, wcount}",
                         d.GetFullName().c_str());
                ret = false;
            }
        }
        return ret;
    }  
};

class AccessCountLoader : public Ladybirds::loadstore::LoadStorableCompound
{
private:
    Ladybirds::spec::TaskGraph &Taskgraph;
    
public:
    AccessCountLoader(Ladybirds::spec::TaskGraph &tg) : Taskgraph(tg) {}
    
    bool LoadStoreMembers(LoadStore &ls) override
    {
        bool ret = true;
        for(auto &t : Taskgraph.Nodes())
        {
            TaskAccessesLoader tal(t);
            ret &= ls.IO(t.Name.c_str(), tal);
        }
        return ret;
    }
};

bool LoadAccesses(Ladybirds::impl::Program &prog, AccessArgs &args)
{
    Ladybirds::lua::LuaEnv lua;
    if(!lua.DoFile(args.Filename.c_str())) return false;
    lua_getglobal(lua, "accesses");
    if(lua_isnil(lua, -1))
    {
        gMsgUI.Error("Access count specification does not contain required 'accesses' table");
        return false;
    }
    
    Ladybirds::lua::LuaLoad load(lua);
    AccessCountLoader acl(prog.TaskGraph);
    return load.RawIO(acl);
}

} // anonymous namespace
