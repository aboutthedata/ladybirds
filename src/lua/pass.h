// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_TOOLS_PASS_H
#define LADYBIRDS_TOOLS_PASS_H

#include <assert.h>
#include <string>
#include <vector>

struct lua_State;

namespace Ladybirds { namespace impl { struct Program; }}
namespace Ladybirds { namespace loadstore { class LoadStorableCompound; }}

namespace Ladybirds {
namespace lua {

/// Represents a pass that can be applied to a program.
/** Upon construction, the object registers itself in a global list
 *  such that it can be made available later on via RegisterPasses.
 *  This class is intended to be instantiated globally and once for every pass.
 *  
 *  Requires and Destroys are used to describe dependencies: Requires is a list of passes that need to have been applied
 *  before this pass can be applied. Destroys is a list of passes the results of which get invalidated by this pass.
 *  Essentially, they are simple string vectors; however, for programming comfort, they are declared as proper classes,
 *  such that the constructor of Pass can be called as <tt> Pass(..., Pass::Requires{...}, Pass::Destroys{...});</tt>
 **/
class Pass
{
public:
    class Requires : public std::vector<std::string> {using std::vector<std::string>::vector;};
    class Destroys : public std::vector<std::string> {using std::vector<std::string>::vector;};
    using Function = bool (*) (impl::Program & prog);
    
private:
    std::string Name_;
    Function Function_;
    Requires Requires_;
    Destroys Destroys_;
    
public:
    Pass(std::string name, Function fn, Requires req = {}, Destroys dest = {});
    virtual ~Pass() {}
    
    const std::string GetName() const { return Name_; }
    
    virtual int Run(lua_State * lua);
    
protected:
    impl::Program & GetProgram(lua_State * lua);
    void CheckDependencies(lua_State * lua, impl::Program & prog);
    void LoadExtraArgs(lua_State * lua, loadstore::LoadStorableCompound & argobj);
    int Finish(lua_State * lua, impl::Program & prog, bool success);
    int Finish(lua_State * lua, impl::Program & prog, loadstore::LoadStorableCompound &retval);
    int Finish(lua_State * lua, impl::Program & prog, std::nullptr_t);
    //TODO: For completeness, more Finish functions with different "return" types should exist
    
private:
    int FinishImpl(lua_State * lua, impl::Program & prog, bool success);
};

/// Inserts a table "passes" in the lua environment given by \p lua. The table contains all passes that are available.
bool RegisterPasses(lua_State * lua);

template<class argT> class PassWithArgs : public Pass
{
public:
    using Function = bool (*) (impl::Program & prog, argT & args);
    
private:
    Function Function_;
    
public:
    PassWithArgs(std::string name, Function fn, Requires req = {}, Destroys dest = {})
        : Pass(std::move(name), nullptr, req, dest), Function_(fn) {}
    
    virtual int Run(lua_State * lua)
    {
        auto & prog = GetProgram(lua);
        CheckDependencies(lua, prog);
        
        argT argobj;
        LoadExtraArgs(lua, argobj);
        
        assert(Function_);
        bool res = (*Function_)(prog, argobj);
        return Finish(lua, prog, res);
    }
};

template<class argT, typename retT> class PassWithArgsAndRet : public Pass
{
public:
    using Function = bool (*) (impl::Program & prog, argT & args, retT &ret);

private:
    Function Function_;
    
public:
    PassWithArgsAndRet(std::string name, Function fn, Requires req = {}, Destroys dest = {})
        : Pass(std::move(name), nullptr, req, dest), Function_(fn) {}
    
    virtual int Run(lua_State * lua)
    {
        auto & prog = GetProgram(lua);
        CheckDependencies(lua, prog);
        
        argT argobj;
        LoadExtraArgs(lua, argobj);
        
        assert(Function_);
        retT retobj;
        bool res = (*Function_)(prog, argobj, retobj);
        return res ? Finish(lua, prog, retobj) : Finish(lua, prog, nullptr);
     }
};



}} //namespace Ladybirds::tools

#endif // LADYBIRDS_TOOLS_PASS_H
