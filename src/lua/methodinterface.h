// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef METHODINTERFACE_H
#define METHODINTERFACE_H

#include <deque>
#include "loadstore.h"

struct lua_State;

namespace Ladybirds { namespace lua {

    
/**
 * @todo write docs
 */
class MethodInterfaceBase
{
protected:
    lua_State *Lua_ = nullptr;

public:
    virtual ~MethodInterfaceBase() = default;
    int LuaInterface(lua_State *lua);
    
protected:
    virtual bool ReadArgs(loadstore::LoadStore &ls) = 0;
    virtual bool Run() = 0;
    virtual int  WriteReturn(loadstore::LoadStore &ls); //default behaviour: Return true
    
    virtual const char* GetTargetTypeString() = 0;
    virtual void SetTarget(loadstore::Referenceable *ptarget) = 0;
    
    void Error(const char *msg, ...);
};


template<class TargetT,
         typename std::enable_if_t<std::is_base_of<loadstore::Referenceable, TargetT>::value>* = nullptr>
class MethodInterface : public MethodInterfaceBase
{
protected:
    TargetT *Target = nullptr;
    
public:
    MethodInterface() = default;
    
private:
    virtual const char *GetTargetTypeString() override { return TargetT::TypeString; }
    virtual void SetTarget(loadstore::Referenceable * ptarget) override { Target = static_cast<TargetT*>(ptarget); }
};


class ObjectMethodsTable
{
private:
    struct Entry
    {
        const char *Name;
        int MemSize;
        MethodInterfaceBase* (*Create)(void*);
        
        template<class T>
        static MethodInterfaceBase* CreateWrapper(void *where) { return new(where) T; }
    };
    std::deque<Entry> Entries_;
    
public:
    ObjectMethodsTable() = default;
    
    template<class T, std::enable_if_t<std::is_base_of<MethodInterfaceBase, T>::value>* = nullptr>
    struct Register
    {
        inline Register(ObjectMethodsTable &table, const char *name) 
            { table.Entries_.push_back(Entry({name, sizeof(T), &Entry::CreateWrapper<T>})); }
    };
    
    void CreateMetaTable(lua_State *lua, const char *typestring);
    
private:
    static int LuaWrapper(lua_State *lua);
};

}} //namespace Ladybirds::lua
#endif // METHODINTERFACE_H
