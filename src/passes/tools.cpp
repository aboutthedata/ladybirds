// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <stdlib.h>
#include <unistd.h>
#include "llvm/Support/Path.h"
#include "llvm/Support/FileSystem.h"
#include "lua/luadump.h"
#include "lua/luaenv.h"
#include "lua/pass.h"
#include "cmdlineoptions.h"

#define CHECK_ERRORS(lua, err, msg, ...) \
    if(err) luaL_error(lua, msg ": %s", __VA_ARGS__, err.message().c_str());

namespace{

using PathString = llvm::SmallString<128>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions

llvm::StringRef GetStringArg(lua_State *lua, int narg)
{
    size_t len;
    const char *str = luaL_checklstring(lua, narg, &len);
    return llvm::StringRef(str, len);
}

inline int PutString(lua_State *lua, llvm::StringRef str)
{
    lua_pushlstring(lua, str.data(), str.size());
    return 1;
}

/// Makes \p path absolute and removes dots. Currently, \p path does not need to exist.
void MakeRealPath(lua_State *lua, PathString &path)
{
    auto err = llvm::sys::fs::make_absolute(path);
    CHECK_ERRORS(lua, err, "Unable to determine real path of '%s'", path.c_str());
    llvm::sys::path::remove_dots(path, true);
}

int MkSymlink(lua_State *lua, const char *from, const char *to)
{
    if(symlink(from, to) == 0) return 0; //success
    
    char err[256];
    (void) !strerror_r(errno, err, sizeof(err)); // There two versions of strerror_r with different return types...
    return luaL_error(lua, "Cannot create symlink from '%s'to '%s': %s", from, to, err);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lua C functions

int RealPath(lua_State * lua)
{
    PathString path = GetStringArg(lua, 1);
    MakeRealPath(lua, path);
    return PutString(lua, path);
}

int BaseName(lua_State *lua)
{
    auto path = GetStringArg(lua, 1);
    return PutString(lua, llvm::sys::path::filename(path));
}

int DirName(lua_State *lua)
{
    auto path = GetStringArg(lua, 1);
    return PutString(lua, llvm::sys::path::parent_path(path));
}

int MkPath(lua_State *lua)
{
    auto path = GetStringArg(lua, 1);
    auto err = llvm::sys::fs::create_directories(path);
    CHECK_ERRORS(lua, err, "Unable to create path '%s'", path.data());
    return 0;
}

/// Creates a symlink from \p from to \p to (1st and 2nd argument).
/// The link is relative to the symlink location (like ln -sr).
int SymLink(lua_State *lua)
{
    PathString from = GetStringArg(lua, 1);
    PathString to = GetStringArg(lua, 2);
    
    MakeRealPath(lua, from); MakeRealPath(lua, to);
    
    auto itfrom = llvm::sys::path::begin(from), itfromend = llvm::sys::path::end(from);
    auto itto = llvm::sys::path::begin(to), ittoend = llvm::sys::path::end(to);
    
    if(*itto != *itfrom)
    { //difference already in first element means we have different file systems, so we must link to absolute path
        return MkSymlink(lua, from.c_str(), to.c_str());
    }
    
    //skip through common path components from the beginning
    auto mypair = std::mismatch(itfrom, itfromend, itto, ittoend);
    itfrom = mypair.first, itto = mypair.second;
    
    if(itto == ittoend)
    {
        return luaL_error(lua, "Trying to symlink '%s' to '%s', one of its parent directories",
                    from.c_str(), to.c_str());
    }
    
    //relative path: count how many directories to go up from 'to' and then add unique part in 'from'
    int up = std::count_if(itto, ittoend, [](auto &) { return true; }) - 1; //the final file name does not need a "../"
    int addlen = itfromend-itfrom;
    
    PathString upstr = llvm::StringRef(".."); // upstr = "../" on linux
    upstr += llvm::sys::path::get_separator();

    PathString relpath; //  relpath = up*upstr + from[itfrom...itfromend]
    relpath.reserve(up*upstr.size()+addlen+1);
    for(int i = 0; i < up; ++i) relpath += upstr;
    relpath += llvm::StringRef(itfrom->data(), addlen);
    
    return MkSymlink(lua, relpath.c_str(), to.c_str());
}

} //namespace ::


/// ToolsPass: Pseudo pass returning a table with helper functions, mainly for the file system
class ToolsPass : public Ladybirds::lua::Pass
{
    using Pass::Pass;
    virtual int Run(lua_State *lua) override
    {
        constexpr luaL_Reg fntable[] = 
        {
            { "realpath",  &RealPath },
            { "basename",  &BaseName },
            { "dirname",   &DirName  },
            { "mkpath",    &MkPath   },
            { "symlink",   &SymLink  },
            { nullptr,     nullptr   }
        };
        
        luaL_newlib(lua, fntable);
        return 1;
    }
};
static ToolsPass MyPass("Tools", nullptr);

/// CmdLinePass: Pseudo pass returning a table with the command line parameters passed to the Ladybirds compiler
class CmdLinePass : public Ladybirds::lua::Pass
{
    using Pass::Pass;
    virtual int Run(lua_State *lua) override
    {
        Ladybirds::lua::LuaDump ld(lua);
        if(ld.RawIO(Ladybirds::tools::gCmdLineOptions)) return 1;
        else return luaL_error(lua, "Unable to export command line arguments to lua environment");
    };
};
static CmdLinePass MyCLPass("CmdLineArgs", nullptr);
