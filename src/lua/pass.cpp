// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "pass.h"

#include <deque>

#include "loadstore.h"
#include "luadump.h"
#include "luaenv.h"
#include "luaload.h"
#include "msgui.h"
#include "program.h"


namespace Ladybirds {
namespace lua {

// This function is a kind of replacement for a global variable. It is the only way to make sure that the pass list
// gets initialized before the Pass objects (which use it). This is called the "construct-on-first-use" pattern.
static auto & GetPassList()
{
    static std::deque<Pass*> passlist;
    return passlist;
}

Pass::Pass(std::string name, Pass::Function fn, Requires req, Destroys dest)
 : Name_(name), Function_(fn), Requires_(req), Destroys_(dest)
{
    GetPassList().push_back(this);
}

impl::Program & Pass::GetProgram(lua_State * lua)
{
    auto getprog = [lua]()
    {
        lua::LuaLoad ll(LuaEnv::FromLuaState(lua));
        impl::Program *pprog;
        if(!ll.RawIOHandle(pprog, &ll))
        {
            lua_pushnil(lua);
            lua_error(lua);
        }
        lua_pop(lua, 1);
        return pprog;
    };
    
    if(!lua_istable(lua, 1))
    {
        lua_pushvalue(lua, 1);
        return *getprog();
    }
    
    lua_pushliteral(lua, "program");
    lua_pushvalue(lua, -1);
    if(lua_rawget(lua, 1) == LUA_TUSERDATA)
    {
        auto pprog = getprog();
        lua_pushnil(lua);
        lua_rawset(lua, 1); //delete from table so we don't confuse the argument parser later on
        return *pprog;
    }
    
    if(lua_rawgeti(lua, 1, 1) == LUA_TUSERDATA)
    {
        auto pprog = getprog();
        lua_pushnil(lua);
        lua_rawseti(lua, 1, 1); //delete from table so we don't confuse the argument parser later on
        return *pprog;
    }
        
    luaL_argerror(lua, 1, "A program object must be passed to the function, either as first element or as 'program='");
    abort(); //this line should never be reached
}

void Pass::CheckDependencies(lua_State * lua, impl::Program & prog)
{
    for(auto & req : Requires_)
    {
        if(prog.PassesPerformed.count(req) == 0)
        {
            luaL_error(lua, "While trying to apply pass '%s': Results of pass '%s' are needed but not available. "
                "Either this pass has not been applied, or its results have been destroyed by another pass.",
                Name_.c_str(), req.c_str());
            assert(false); //this code should never be reached
        }
    }
    for(auto & dest : Destroys_)
    {
        //already delete them now, since the results from previous passes may be invalid also if an error occurs
        prog.PassesPerformed.erase(dest);
    }
}

void Pass::LoadExtraArgs(lua_State * lua, loadstore::LoadStorableCompound & argobj)
{
    if(lua_istable(lua, 1)) lua_settop(lua, 1);
    else if(lua_istable(lua, 2)) lua_settop(lua, 2);
    else luaL_argerror(lua, 1, "either the first or the second argument must be a table containing the pass parameters");
    
    lua::LuaLoad ll(lua::LuaEnv::FromLuaState(lua));
    if(!ll.RawIO(argobj))
    {
        lua_pushnil(lua); //no error message, since RawIO should already have complained
        lua_error(lua);
    }
}

int Pass::FinishImpl(lua_State *lua, impl::Program &prog, bool success)
{
    if(success) prog.PassesPerformed.insert(Name_);
    return 1;
}


int Pass::Finish(lua_State *lua, impl::Program &prog, bool success)
{
    lua_pushboolean(lua, success);
    return FinishImpl(lua, prog, success);
}


int Pass::Finish(lua_State *lua, impl::Program &prog, loadstore::LoadStorableCompound &retval)
{
    LuaDump ld(lua);
    bool success = ld.RawIO(retval);
    return FinishImpl(lua, prog, success);
}


int Pass::Finish(lua_State *lua, impl::Program & prog, std::nullptr_t)
{
    lua_pushnil(lua);
    return FinishImpl(lua, prog, false);
}

int Pass::Run(lua_State * lua)
{
    auto & prog = GetProgram(lua);
    CheckDependencies(lua, prog);
    
    assert(Function_);
    bool res = (*Function_)(prog);

    return Finish(lua, prog, res);
}

static int LuaPassInterface(lua_State * lua)
{
    void * p = lua_touserdata(lua, lua_upvalueindex(1));
    return static_cast<Pass*>(p)->Run(lua);
}

bool RegisterPasses(lua_State * lua)
{
    auto & passlist = GetPassList();
    lua_createtable(lua, 0, passlist.size());
    for(auto ppass : passlist)
    {
        lua_pushlightuserdata(lua, ppass);
        lua_pushcclosure(lua, &LuaPassInterface, 1);
        lua_setfield(lua, -2, ppass->GetName().c_str());
    }
    lua_setglobal(lua, "Ladybirds");
    return true;
}

}} //namespace Ladybirds::tools
