// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "methodinterface.h"

#include <string.h>
#include "lua/luadump.h"
#include "lua/luaenv.h"
#include "lua/luaload.h"

namespace Ladybirds { namespace lua {

void ObjectMethodsTable::CreateMetaTable(lua_State* lua, const char *typestring)
{
    char buf[128] = "handle:";
    strcpy(buf+7, typestring);
    if(luaL_newmetatable(lua, buf) == 0) return lua_pop(lua, 1); //table already exists
    
    for(auto &entry : Entries_)
    {
        lua_pushlightuserdata(lua, &entry);
        lua_pushcclosure(lua, &LuaWrapper, 1);
        lua_setfield(lua, -2, entry.Name);
    }
    lua_setfield(lua, -1, "__index"); //set index lookup to that table such that methods are available on any object
}

int ObjectMethodsTable::LuaWrapper(lua_State* lua)
{
    auto pentry = static_cast<Entry*>(lua_touserdata(lua, lua_upvalueindex(1)));
    assert(pentry);
    
    class stackalloc
    {public:
        MethodInterfaceBase *pobj;
     
        stackalloc(MethodInterfaceBase *p) : pobj(p) {}
        ~stackalloc() {pobj->~MethodInterfaceBase();}
    } iface(pentry->Create(alloca(pentry->MemSize)));

    return iface.pobj->LuaInterface(lua);
}

int MethodInterfaceBase::LuaInterface(lua_State* lua)
{
    Lua_ = lua;
    
    lua_settop(lua, 2); //eliminate additional parameters and clean up stack
    lua_rotate(lua, 1, 1); //let's start with the first parameter: the target object
    
    LuaLoad ll(LuaEnv::FromLuaState(lua));
    loadstore::Referenceable *ptarget;
    if(!ll.RawIOHandle(ptarget, nullptr, GetTargetTypeString(), true))
        return luaL_error(lua, "This method must be called on a %s handle", GetTargetTypeString());
    SetTarget(ptarget);
    
    if(!ReadArgs(ll)) return luaL_error(lua, "Invalid or missing arguments for this method");
    
    if(!Run()) return 0; //Just return nil here. If something really bad happens, the interface should call Error()
    
    LuaDump ld(lua);
    return WriteReturn(ld);
}

void MethodInterfaceBase::Error(const char* msg, ...)
{
    va_list va;
    va_start(va, msg);
    lua_pushvfstring(Lua_, msg, va);
    lua_error(Lua_);
}

int MethodInterfaceBase::WriteReturn(loadstore::LoadStore &ls)
{
    bool b = true;
    ls.RawIO(b);
    return 1;
}

}} //namespace Ladybirds::lua
