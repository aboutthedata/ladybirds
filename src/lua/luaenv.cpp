// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "luaenv.h"

#include <cstring>
#include <iostream>
#include <fstream>
#include <memory>
#include "msgui.h"
#include "simplearray.h"
#include "tools.h"

using namespace std;

namespace {

SimpleArray<char> GetFileContents(const char *filename)
{
    std::ifstream in(filename, ios::in | ios::binary | ios::ate);
    if (!in) return SimpleArray<char>(-1);

    int length = in.tellg();
    SimpleArray<char> contents(length);
    
    in.seekg(0, std::ios::beg);
    in.read(contents.data(), length);
    in.close();

    return contents;
}

} //namespace ::

namespace Ladybirds { namespace lua {

LuaEnv::LuaEnv()
{
    if(!(State_ = luaL_newstate()))
    {
        cerr << "Fatal error: Unable to open Lua environment. Is enough memory available?" << endl;
        exit(1);
    }
    luaL_openlibs(State_);
    
    // Store a pointer to this object in the lua environment for the FromLuaState function.
    // The address of the latter is also used in lua_rawsetp, but only as a unique identifier.
    // Therefore, the reinterpret_cast is OK.
    lua_pushlightuserdata(State_, this);
    lua_rawsetp(State_, LUA_REGISTRYINDEX, reinterpret_cast<void*>(&FromLuaState));
}

LuaEnv::~LuaEnv()
{
    lua_close(State_);
    State_ = nullptr;
}


void LuaEnv::ReportErrors(const char * extraMsg /*=nullptr*/)
{
    assert(State_);
    
    if(!lua_isnil(State_, -1)) //otherwise, we assume the error has already been printed
        gMsgUI.Error(extraMsg ? extraMsg : "An error occured in Lua:")
            << luaM_tostdstring(State_, -1) << std::endl;

    lua_pop(State_, 1); // remove error message
}

static int VerboseErrorHandler(lua_State * lua)
{
    luaL_traceback(lua, lua, "\n", 1);
    lua_concat(lua, 2);
    return 1;
}

bool LuaEnv::ExecuteCode(const char* code, int len, const char* chunkname, const char* errorMsg)
{
    bool verbose = gMsgUI.IsVerbose();
    
    if(verbose) lua_pushcfunction(State_, &VerboseErrorHandler);
    int ret = luaL_loadbuffer(State_, code, len, chunkname)
           || lua_pcall(State_, 0, LUA_MULTRET, verbose ? -2 : 0);
    if(ret) ReportErrors(errorMsg);
    return !ret;
}


bool LuaEnv::DoFile(const char* filepath, const char * errorMsg /*=nullptr*/)
{
    assert(State_);
    
    Code_ = GetFileContents(filepath);
    if(!Code_.data())
    {
        perror(filepath);
        return false;
    }
    CodeLines_.clear();
    CodeLines_.push_back(Code_.data());
    
    bool ret = ExecuteCode(Code_.data(), Code_.size(), (string("@") + filepath).c_str(), errorMsg);
    
    Code_.clear();
    return ret;
}

bool LuaEnv::DoString(const char* str, int len, const char* errorMsg)
{
    return ExecuteCode(str, len, "<anonymous code>", errorMsg);
}


string LuaEnv::GetCodeLine(int line)
{
    assert(line > 0);
    if(line >= static_cast<int>(CodeLines_.size()))
    {//have to load the line indices first
        char * pline = CodeLines_.back();
        char * docend = Code_.end();
        for(int i = CodeLines_.size(); i <= line; i++)
        {
            pline = static_cast<char*>(memchr(pline, '\n', docend-pline));
            if(!pline)
            {
                if(i < line) return string(); // tried past eof access
                else pline = docend;
            }
            else ++pline;//line begins after \n character
            
            CodeLines_.push_back(pline);
        }
    }
    char * pout = CodeLines_[line-1];
    return string(pout, CodeLines_[line] - pout);
}

LuaEnv & LuaEnv::FromLuaState(lua_State* pstate)
{
    //The address of this function is just used as a unique identifier, so the reinterpret_cast is OK.
    if(lua_rawgetp(pstate, LUA_REGISTRYINDEX, reinterpret_cast<void*>(&FromLuaState)) != LUA_TLIGHTUSERDATA)
        assert(false && "Failure retrieving LuaEnv object. Was this state created using a proper LuaEnv constructor?");
    void * p = lua_touserdata(pstate, -1);
    lua_pop(pstate, 1);

    assert(p);
    return *static_cast<LuaEnv*>(p);
}

}} //namespace Ladybirds::lua
