// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LUALOAD_H
#define LUALOAD_H

#include <string>

#include "loadstore.h"

namespace Ladybirds { namespace lua {

class LuaEnv;

class LuaLoad : public loadstore::LoadStore
{
private:
    LuaEnv & Lua_;
    int NumErrors_ = 0;
    int ErrorIndex_ = 0;
    
    void * LastRegistered_ = nullptr;
    const char * LastRegType_ = nullptr;

public:
    inline LuaLoad(LuaEnv & lua) : LoadStore(LoadStore::Load), Lua_(lua) {}
    inline ~LuaLoad() {}
    
    LuaLoad(const LuaLoad& other) = delete;
    LuaLoad& operator=(const LuaLoad& other) = delete;//not allowed
    
    virtual bool PrepareNamedVar(const char* name, bool showErrorMsg) override;
    
    virtual bool RawArrayIO(int nItems, std::function<bool(LoadStore &)> callback) override;
    virtual bool RawMapIO(int nItems, std::function<bool(std::string &, LoadStore &)> callback) override;
    virtual bool RawIO(loadstore::LoadStorableCompound& var) override;
    virtual bool RawIO(std::string& var) override;
    virtual bool RawIO(bool& var) override;
    virtual bool RawIO(int& var) override;
    virtual bool RawIO(double& var) override;
    virtual bool RawIORef(loadstore::Referenceable *& ref, const char *type, bool required) override;
    virtual bool RawIOHandle(loadstore::Referenceable *& ref, const void *context, const char *type, bool required) override;
    virtual bool RawIO_Register(loadstore::Referenceable& obj) override;

    using LoadStore::RawIORef;
    using LoadStore::RawIOHandle;
    
    
    bool ExtractIdentifier(std::string &out);
    bool PushLastRegistered();
    
    virtual void Error(const char* msg, ... ) override;
    inline int GetErrorCount() {return NumErrors_;}
    inline void ChangeErrorIndex(int offset) {ErrorIndex_ += offset;}
};


}} //namespace Ladybirds::lua

#endif // LUALOAD_H
