// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "luaload.h"

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <utility>
#include <regex>
#include "luaenv.h"
#include "tools.h"

namespace Ladybirds { namespace lua {


using namespace std;
namespace{
//! \internal Helper that automatically calls lua_pop at the end of a function
class AutoPop : public std::pair<lua_State *, int>
{
public:
    using std::pair<lua_State *, int>::pair;
    inline ~AutoPop() {lua_pop(first, second);}
};
} //namespace::

bool LuaLoad::PrepareNamedVar(const char* name, bool showErrorMsg)
{
    assert(lua_istable(Lua_, -1));
    lua_pushstring(Lua_, name);
    lua_rawget(Lua_, -2);
    if(lua_isnil(Lua_, -1))
    {
        lua_pop(Lua_, 1);
        if(showErrorMsg) Error("Parameter '%s' not found", name);
        return false;
    }
    return true;
}

bool LuaLoad::RawIO(bool& var)
{
    AutoPop ap(Lua_, 1);
    if(!lua_isboolean(Lua_, -1))
    {
        Error("Boolean expected, got %s", lua_typename(Lua_, lua_type(Lua_,-1)));
        return false;
    }
    var = lua_toboolean(Lua_, -1);
    return true;
}


bool LuaLoad::RawIO(int& var)
{
    static_assert(std::is_same<lua_Number, double>(), "Definition of lua_Number changed");
    AutoPop ap(Lua_, 1);
    
    double input = lua_tonumber(Lua_, -1);
    if(input == 0 && !lua_isnumber(Lua_, -1))
    {
        Error("Integer expected, got %s", lua_typename(Lua_, lua_type(Lua_,-1)));
        return false;
    }
    
    double rounded = std::round(input);
    if(std::abs(input - rounded) > 1e-10*std::abs(input))
    {
        Error("Integer expected, got double");
        return false;
    }
    
    if(rounded > INT_MAX || rounded < INT_MIN)
    {
        Error("Integer out of bounds");
        return false;
    }
    
    var = int(rounded);
    return true;
}

bool LuaLoad::RawIO(double& var)
{
    static_assert(std::is_same<lua_Number, double>(), "Definition of lua_Number changed");
    AutoPop ap(Lua_, 1);
    
    double input = lua_tonumber(Lua_, -1);
    if(input == 0 && !lua_isnumber(Lua_, -1))
    {
        Error("Number expected, got %s", lua_typename(Lua_, lua_type(Lua_,-1)));
        return false;
    }
    
    var = input;
    
    return true;
}

bool LuaLoad::RawIO(std::string& var)
{
    AutoPop ap(Lua_, 1);
    size_t len;
    const char * input = lua_tolstring(Lua_, -1, &len);
    if(!input)
    {
        Error("String expected, got %s", lua_typename(Lua_, lua_type(Lua_,-1)));
        return false;
    }
    
    var.assign(input, len);
    
    return true;
}

bool LuaLoad::RawIO(loadstore::LoadStorableCompound& var)
{
    auto top = lua_gettop(Lua_);
    if(!lua_istable(Lua_, -1))
    {
        if(var.LoadFromShortcut(*this)) return true;
        
        Error("Compound object expected, got %s", lua_typename(Lua_, lua_type(Lua_,-1)));
        lua_settop(Lua_, top-1);
        return false;
    }
    bool ret = var.LoadStoreMembers(*this) ? true:false;//Make sure to return either 1 or 0, s.t. the "&" operator works
    lua_settop(Lua_, top-1);
    return ret;
}

bool LuaLoad::RawIORef(loadstore::Referenceable*& ref, const char* type, bool required)
{
    assert(false && "Not implemented"); return false;
}


bool LuaLoad::RawIOHandle(loadstore::Referenceable*& ref, const void *context, const char* type, bool required)
{
    AutoPop ap(Lua_, 1);
    if(lua_type(Lua_, -1) != LUA_TUSERDATA)
    {
        Error("Object handle expected, got %s", lua_typename(Lua_, lua_type(Lua_,-1)));
        return false;
    }
    if(!lua_getmetatable(Lua_, -1) || lua_rawgeti(Lua_, -1, 1) != LUA_TLIGHTUSERDATA)
    {
        Error("Argument is not a valid object handle.");
        return false;
    }
    
    auto argtype = static_cast<const char *>(lua_touserdata(Lua_, -1));
    lua_pop(Lua_, 2);
    if(argtype != type)
    {
        if(!argtype) argtype = "object of unknown type";
        
        Error("%s expected, got %s", type, argtype);
        return false;
    }
    
    auto len = lua_rawlen(Lua_, -1);
    assert(len >= 2*sizeof(void*));

    auto udata = static_cast<void**>(lua_touserdata(Lua_, -1));
    if(udata == nullptr)
    {
        Error("Invalid %s. (Check previous errors.)", type);
        return false;
    }
    
    if(context && context != this && udata[0] != context)
    {
        Error("Wrong context of object handle. "
              "Ensure the object was created for the context in which it is used now.");
        return false;
    }
    ref = static_cast<loadstore::Referenceable*>(udata[1]);
    assert(strcmp(ref->GetTypeString(), type) == 0 && "Invalid object handle");        
    
    return true;
}

bool LuaLoad::RawIO_Register(loadstore::Referenceable& obj)
{
    //normally load object
    bool ret = RawIO(obj);
    
    //then store a reference to it for later use in PushLastRegistered.
    //If loading didn't work, still put an invalid (nullptr) value
    LastRegistered_ = ret ? &obj : nullptr;
    LastRegType_ = obj.GetTypeString();
    
    return ret;
}



bool LuaLoad::RawArrayIO(int nItems, std::function<bool(LoadStore &)> callback)
{
    AutoPop ap(Lua_, 1);
    if(!lua_istable(Lua_, -1))
    {
        Error("Array expected, got %s", lua_typename(Lua_, lua_type(Lua_,-1)));
        return false;
    }
    
    bool ret = true;
    lua_pushnil(Lua_);
    DEBUG_CODE(int top = lua_gettop(Lua_));
    while (lua_next(Lua_, -2) != 0)
    {
        if(!callback(*this)) ret = false;
        assert(lua_gettop(Lua_) == top);
    }
    
    return ret;
}

bool LuaLoad::RawMapIO(int nItems, std::function<bool(std::string &, LoadStore &)> callback)
{
    AutoPop ap(Lua_, 1);
    if(!lua_istable(Lua_, -1))
    {
        Error("Table expected, got %s", lua_typename(Lua_, lua_type(Lua_,-1)));
        return false;
    }
    
    bool ret = true;
    lua_pushnil(Lua_);
    int top = lua_gettop(Lua_);
    while (lua_next(Lua_, -2) != 0)
    {
        lua_pushvalue(Lua_, -2);
        bool error = false;
        auto key = luaM_tostdstring(Lua_, -1, &error);
        lua_pop(Lua_, 1);
        if(error)
        {
            Error("Object of type %s cannot serve as a key for a table to be read in",
                  lua_typename(Lua_, lua_type(Lua_,-2)));
            ret = false;
            lua_settop(Lua_, top);
        }
        else
        {
            if(!callback(key, *this)) ret = false;
            assert(lua_gettop(Lua_) == top);
        }
    }
    
    return ret;
}

void LuaLoad::Error(const char* msg, ... )
{
    constexpr int limit = 2000;
    if(++NumErrors_ > limit)
    {
        fprintf(stderr, "More than %d errors. Aborted translation.\n", limit);
        exit(1);
    }
    
    lua_Debug curfunc, caller;
    if(lua_getstack(Lua_, ErrorIndex_+0, &curfunc))
    {
        lua_getinfo(Lua_, "n", &curfunc);
        lua_getstack(Lua_, ErrorIndex_+1, &caller) && lua_getinfo(Lua_, "Sl", &caller);
    
        fprintf(stderr, "%s:%d: Error in %s: ", caller.short_src, caller.currentline, curfunc.name);
    }
    else fprintf(stderr, "%s: Error: ", Lua_.GetLastSource().c_str());
    va_list args; va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fputc('\n', stderr);
}


bool LuaLoad::ExtractIdentifier(string& out)
{
    lua_Debug caller;
    lua_getstack(Lua_, ErrorIndex_+1, &caller);
    lua_getinfo(Lua_, "l", &caller);
    string curline = Lua_.GetCodeLine(caller.currentline);
    
    std::regex re("^\\s*([[:alnum:]]\\w*)\\s*=");
    
    std::smatch match;
    if(!std::regex_search(curline, match, re)) return false;
    out = match[1].str();
    return true;
}

bool LuaLoad::PushLastRegistered()
{
    if(!LastRegType_) return false;

    luaM_pushpointer(Lua_, LastRegistered_);
    luaL_newmetatable(Lua_, LastRegType_); //Creates a new or loads the existing metatable.
    lua_setmetatable(Lua_, -2);
    LastRegistered_ = nullptr;
    LastRegType_ = nullptr;
    return true;
}

}} //namespace Ladybirds::lua
