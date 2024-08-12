// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_LUA_LUAINTERFACE_H
#define LADYBIRDS_LUA_LUAINTERFACE_H

#include <initializer_list>
#include <string>
#include <utility>
#include "luaenv.h"

namespace Ladybirds {
namespace lua {


template<class t>
class LuaInterface
{
public:
    typedef int (t::*MethodPtr)(lua_State *);
    struct ExportableMethod
    {
        const char *Name;
        MethodPtr Function;
    };
    
private:
    lua_State *Lua_;
    
public:
    LuaInterface(lua_State *lua, std::initializer_list<ExportableMethod> methods)
      : Lua_(lua)
    {
        if(luaL_newmetatable(lua, t::GetLuaName()) == 0) return; //Interface has already been registered
        lua_createtable(lua, 0, methods.size());
        for(auto &m : methods)
        {
            void *p = lua_newuserdata(lua, sizeof(m.Function));
            *reinterpret_cast<MethodPtr*>(p) = m.Function;
            
            lua_pushcclosure(lua, &CallWrapper, 1);
            lua_setfield(lua, -2, m.Name);
        }
        lua_setfield(lua, -2, "__index");
        lua_pushcfunction(lua, &DestructorWrapper);
        lua_setfield(lua, -2, "__gc");
        lua_pop(lua, 1);
    }
    
    template< class... Args >
    int CreateObject(Args&&... args)
    {
        auto *p = lua_newuserdata(Lua_, sizeof(t));
        new (p) t(std::forward<Args>(args)...);
        
        luaL_getmetatable(Lua_, t::GetLuaName());
        assert(!lua_isnil(Lua_, -1) && "Metatable not found");
        lua_setmetatable(Lua_, -2);
        return 1;
    }
    
private:
    static int CallWrapper(lua_State *lua)
    {
        void *upval = lua_touserdata(lua, lua_upvalueindex(1));
        assert(upval && "Method address not supplied.. Was the method registered correctly?");
        
        auto thisobj = static_cast<t*>(luaL_checkudata(lua, 1, t::GetLuaName()));
        return (thisobj->*(*static_cast<MethodPtr*>(upval)))(lua);
    }
    static int DestructorWrapper(lua_State *lua)
    {
        auto thisobj = static_cast<t*>(luaL_checkudata(lua, 1, t::GetLuaName()));
        thisobj->~t();
        return 0;
    }
};

    
}} //namespace Ladybirds::lua

#endif // LADYBIRDS_LUA_LUAINTERFACE_H
