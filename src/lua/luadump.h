// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LUADUMP_H
#define LUADUMP_H

#include <unordered_map>

#include "loadstore.h"
#include "luaenv.h"

struct lua_State;

namespace Ladybirds { namespace lua {

class LuaDump : public loadstore::LoadStore
{
    using Referenceable = loadstore::Referenceable;
    enum HandleType { Handle = 0, Managed, LuaMem };
    
private:
    lua_State * Lua_;
    int NumErrors_ = 0;
    int ErrorIndex_ = 0;
    std::unordered_map<void*, const char*> TempObjects_;

public:
    inline LuaDump(lua_State * lua) : LoadStore(LoadStore::Store), Lua_(lua) {}
    ~LuaDump();
    
    LuaDump(const LuaDump& other) = delete;//not allowed
    LuaDump& operator=(const LuaDump& other) = delete;//not allowed
    
    virtual bool PrepareNamedVar(const char* name, bool showErrorMsg) override;
    virtual bool FlushNamedVar (const char *name, bool showErrorMsg = true) override;
    
    virtual bool RawArrayIO(int nItems, std::function<bool(LoadStore &)> callback) override;
    virtual bool RawMapIO(int nItems, std::function<bool(std::string &, LoadStore &)> callback) override;
    virtual bool RawIO(loadstore::LoadStorableCompound& var) override;
    virtual bool RawIO(std::string& var) override;
    virtual bool RawIO(bool& var) override;
    virtual bool RawIO(int& var) override;
    virtual bool RawIO(double& var) override;
    virtual bool RawIORef(Referenceable *& ref, const char * type, bool required) override;
    virtual bool RawIO_Register(Referenceable& obj) override;
    virtual bool RawIOHandle(Referenceable *& ref, const void *context, const char *type, bool required) override;

    template<class T, typename... argsT, typename std::enable_if_t<std::is_base_of<Referenceable, T>::value>* = nullptr>
    T* CreateManaged(argsT... args)
    {
        auto udata = static_cast<void**>(lua_newuserdata(Lua_, sizeof(void*)*2 + sizeof(T)));
        udata[0] = nullptr;
        auto pobj = new(udata+2) T(std::forward<argsT>(args)...);
        udata[1] = static_cast<Referenceable*>(pobj);
        
        PushMetatable(pobj->GetTypeString(), LuaMem);
        lua_setmetatable(Lua_, -2);
        return pobj;
    }
    
    virtual void Error(const char* msg, ... ) override;
    inline int GetErrorCount() {return NumErrors_;}
    inline void ChangeErrorIndex(int offset) {ErrorIndex_ += offset;}
    
private:
    void PushMetatable(const char *typestring, HandleType htype);
};

}} //namespace Ladybirds::lua

#endif // LUADUMP_H
