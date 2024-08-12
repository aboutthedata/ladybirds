// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "luadump.h"

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <utility>

#include "luaenv.h"
#include "msgui.h"
#include "tools.h"

namespace Ladybirds { namespace lua {

using std::vector;
using std::string;


LuaDump::~LuaDump()
{
    for(auto & entry : TempObjects_)
    {
        gMsgUI.Warning("LuaDump: Unresolved object : %p (%s)", entry.first, entry.second);
    }
}

bool LuaDump::PrepareNamedVar(const char* name, bool showErrorMsg)
{
    assert(lua_istable(Lua_, -1));
    lua_pushstring(Lua_, name);
    return true;
}

bool LuaDump::FlushNamedVar(const char *name, bool showErrorMsg)
{
    assert(lua_type(Lua_, -2) == LUA_TSTRING && lua_type(Lua_, -3) == LUA_TTABLE);
    assert(luaM_tostdstring(Lua_, -2) == name);
    lua_rawset(Lua_, -3);
    return true;
}


bool LuaDump::RawIO(bool& var)
{
    lua_pushboolean(Lua_, var);
    return true;
}


bool LuaDump::RawIO(int& var)
{
    static_assert(sizeof(int) <= sizeof(lua_Integer), "Definition of lua_Integer changed");
    lua_pushinteger(Lua_, var);
    return true;
}

bool LuaDump::RawIO(double& var)
{
    static_assert(std::is_same<lua_Number, double>(), "Definition of lua_Number changed");
    lua_pushnumber(Lua_, var);
    return true;
}

bool LuaDump::RawIO(std::string& var)
{
    lua_pushlstring(Lua_, var.c_str(), var.size());
    return true;
}

bool LuaDump::RawIO(loadstore::LoadStorableCompound& var)
{
    lua_newtable(Lua_);
    if(!var.LoadStoreMembers(*this))
    {
        assert(lua_type(Lua_, -1) == LUA_TTABLE);
        return false;
    }
    return true;
}

bool LuaDump::RawIORef(loadstore::Referenceable*& ref, const char* type, bool required)
{
    lua_pushlightuserdata(Lua_, ref);
    lua_rawget(Lua_, LUA_REGISTRYINDEX);
    if(lua_isnil(Lua_, -1))
    {
        if(!ref)
        {
            //leave one "nil" on the stack for the "FlushNamedVar" function later on
            if(!required) return true;
            
            Error("Tried to reference null pointer.");
            return false;
        }
        
        //save a dummy table for now, which is filled later when the object referenced here is registered.
        lua_pop(Lua_, 1);
        lua_newtable(Lua_);
        lua_pushlightuserdata(Lua_, ref);
        lua_pushvalue(Lua_, -2);
        lua_rawset(Lua_, LUA_REGISTRYINDEX);
        
        TempObjects_.emplace(ref, type);
    }
    return true;
}

bool LuaDump::RawIO_Register(loadstore::Referenceable& obj)
{
    //check if obj has already been referenced, in this case we already have a dummy table
    lua_pushlightuserdata(Lua_, &obj);
    lua_rawget(Lua_, LUA_REGISTRYINDEX);
    if(!lua_isnil(Lua_, -1))
    {
        //now we are resolving that dummy, so remove it from the temp object list
        TempObjects_.erase(&obj);
    }
    else
    {
        lua_pop(Lua_, 1);
        
        //if not: create a new table and register it
        lua_newtable(Lua_);
        lua_pushlightuserdata(Lua_, &obj);
        lua_pushvalue(Lua_, -2);
        lua_rawset(Lua_, LUA_REGISTRYINDEX);
    }

    //set metatable
    luaL_getmetatable(Lua_, obj.GetTypeString()); //Loads the metatable for obj (or nil).
    if(lua_isnil(Lua_, -1)) lua_pop(Lua_, 1);
    else lua_setmetatable(Lua_, -2);
    
    //now, take care of the members
    if(!obj.LoadStoreMembers(*this))
    {
        assert(lua_type(Lua_, -1) == LUA_TTABLE);
        return false;
    }
    return true;
}

static int DeleteWrapper(lua_State *lua)
{
    assert(lua_type(lua, 1) == LUA_TUSERDATA);
    auto udata = static_cast<void**>(lua_touserdata(lua, 1));
    assert(udata);
    assert(udata[0] == nullptr);
    delete static_cast<loadstore::Referenceable*>(udata[1]);
    return 0;
}

static int DestructorWrapper(lua_State *lua)
{
    assert(lua_type(lua, 1) == LUA_TUSERDATA);
    auto udata = static_cast<void**>(lua_touserdata(lua, 1));
    assert(udata);
    assert(udata[0] == nullptr);
    static_cast<loadstore::Referenceable*>(udata[1])->~Referenceable();
    return 0;
}

void LuaDump::PushMetatable(const char *typestring, HandleType htype)
{
    static const string prefixes[] = {"handle:", "managed:", "luamem:"};

    if(luaL_newmetatable(Lua_, (prefixes[htype]+typestring).c_str()) == 1)
    {
        lua_pushlightuserdata(Lua_, const_cast<char*>(typestring));
        lua_rawseti(Lua_, -2, 1);
        switch(htype)
        {
            case Handle:
                return;
            case Managed:
                lua_pushcfunction(Lua_, &DeleteWrapper);
                break;
            case LuaMem:
                lua_pushcfunction(Lua_, &DestructorWrapper);
                break;
        }
        lua_setfield(Lua_, -2, "__gc");
        if(luaL_getmetatable(Lua_, (prefixes[Handle]+typestring).c_str()) == LUA_TTABLE)
            lua_setfield(Lua_, -2, "__index");
        else lua_pop(Lua_, 1);
    }
}

bool LuaDump::RawIOHandle(loadstore::Referenceable *&ref, const void *context, const char* type, bool required)
{
    auto udata = static_cast<const void**>(lua_newuserdata(Lua_, sizeof(void*)*2));
    udata[0] = (context == this) ? nullptr : context;
    udata[1] = ref;
    
    PushMetatable(type, context == this ? Managed : Handle);
    lua_setmetatable(Lua_, -2);
    return true;
}

bool LuaDump::RawArrayIO(int nItems, std::function<bool(LoadStore &)> callback)
{
    lua_createtable(Lua_, nItems, 0);
    
    bool ret = true;
    DEBUG_CODE(int top = lua_gettop(Lua_));
    for(int i = 1; i <= nItems; i++)
    {
        ret = callback(*this) && ret;
        assert(lua_istable(Lua_, -2));
        lua_rawseti(Lua_, -2, i);
        assert(lua_gettop(Lua_) == top);
    }
    
    return ret;
}

bool LuaDump::RawMapIO(int nItems, std::function<bool(std::string &, LoadStore &)> callback)
{
    lua_createtable(Lua_, nItems, 0);
    
    bool ret = true;
    std::string key;
    DEBUG_CODE(int top = lua_gettop(Lua_));
    for(int i = 1; i <= nItems; i++)
    {
        if(!callback(key, *this))
        {
            ret = false;
        }
        else
        {
            assert(lua_istable(Lua_, -2));
            lua_pushlstring(Lua_, key.c_str(), key.length());
            lua_rotate(Lua_, -2, 1);
            lua_rawset(Lua_, -3);
        }
        assert(lua_gettop(Lua_) == top);
    }
    
    return ret;
}

void LuaDump::Error(const char* msg, ... )
{
    constexpr int limit = 2000;
    if(++NumErrors_ > limit)
    {
        fprintf(stderr, "More than %d errors. Aborted translation.\n", limit);
        exit(1);
    }
    
    fputs("Error: ", stderr);
    va_list args; va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fputc('\n', stderr);
}

}} //namespace Ladybirds::lua
