// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LUAENV_H
#define LUAENV_H

#include "lua.hpp"

#include <assert.h>
#include <string>
#include <vector>

#include "simplearray.h"

//Compatibility for lua 5.1
#if LUA_VERSION_NUM < 502
#define lua_pushglobaltable(state) lua_pushvalue(state, LUA_GLOBALSINDEX)
#endif



namespace Ladybirds { namespace lua {

//! Encapsulation of Lua lua_State pointer. Uses RAII.
/** Can be used in lua_* functions as a replacement argument for a state pointer.**/
class LuaEnv
{
private:
    lua_State * State_;
    SimpleArray<char> Code_;
    std::vector<char *> CodeLines_;
    std::string LastSource_;

public:
    //! Opens a new Lua environment ("State") and loads all standard lua libraries.
    /** Reports an error and calls exit(1) on failure.**/
    LuaEnv();
    
    LuaEnv(const LuaEnv& other) = delete;//forbidden
    LuaEnv(LuaEnv && other) = default;

    //! Closes the Lua environment.
    ~LuaEnv();
    
    LuaEnv& operator=(const LuaEnv& other) = delete;
    
    //! Returns the lua_State needed for all the lua_* functions.
    inline operator lua_State*() { assert(State_); return State_;}
    
    //! Gives out an error message to stderr if something went wrong.
    /** This function must only be called after a toplevel Lua function (e.g. lua_pcall) returned an error.**/
    void ReportErrors(const char * extraMsg = nullptr);
    
    //! Executes the lua file given by filepath.
    /** On error, displays an error message on stderr, adding errorMsg if given, and returns false.**/
    bool DoFile(const char * filepath, const char * errorMsg = nullptr);

    //! Executes the string given by \p str.
    /** On error, displays an error message on stderr, adding errorMsg if given, and returns false.**/
    template<int len>
    bool DoString(const char (&str)[len], const char * errorMsg = nullptr)
        { return DoString(str, str[len-1] == 0 ? len-1 : len, errorMsg); }
    
    /**\copydoc DoString. The length of \p str must be given by \p len. **/
    bool DoString(const char *str, int len, const char * errorMsg = nullptr);

    //! Returns the contents of line number \p line of the code file currently executed
    std::string GetCodeLine(int line);
    
    //! Returns the name of the last file that was executed using DoFile
    inline const std::string & GetLastSource() const { return LastSource_; }
    
    //! Given a lua state that was created by a LuaEnv object, returns a reference to that LuaEnv object.
    static LuaEnv & FromLuaState(lua_State * pstate);
    
private:
    bool ExecuteCode(const char *code, int len, const char* chunkname, const char* errorMsg);
};

//! Like lua_tostring, but returns an std::string.
/** On error returns an empty string and sets \p *perror to true if given.**/
inline std::string luaM_tostdstring(lua_State * lua, int index, /*out*/ bool * perror = nullptr)
{
    size_t len=0;
    const char * pstr = lua_tolstring(lua, index, &len);
    if(!pstr)
    {
        if(perror) *perror = true;
        return std::string();
    }
    return std::string(pstr, len);
}

//! Pushes a userdata object on the stack and stores \p ptr in it
inline void luaM_pushpointer(lua_State * lua, void * ptr)
{
    auto luaref = static_cast<void**>(lua_newuserdata(lua, sizeof(void*)));
    *luaref = ptr;
}

}} //namespace Ladybirds::lua

#endif // LUAENV_H
