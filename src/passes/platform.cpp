// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <fstream>

#include "graph/graph-dump.h"
#include "lua/methodinterface.h"
#include "lua/luadump.h"
#include "lua/pass.h"
#include "spec/platform.h"
#include "tools.h"

using Ladybirds::spec::Platform;
using Ladybirds::loadstore::LoadStore;
using Ladybirds::lua::ObjectMethodsTable;
using Ladybirds::lua::MethodInterface;

static ObjectMethodsTable PlatformIface;

#define concat_(a, b) a##b
#define concat(a, b) concat_(a,b)
#define REGUID concat(reg, __LINE__)


template<class T, T* (Platform::*AddObject)(T&&)>
class AddItemMethod : public MethodInterface<Platform>
{
protected:
    T obj;
    T *pret;
    
    virtual bool ReadArgs(LoadStore &ls) override
    {
        ls.UserContext = Target;
        return ls.RawIO(obj);
    }
    virtual bool Run() override
    {
        pret = (Target->*AddObject)(std::move(obj));
        return true;
    }
    virtual int WriteReturn(LoadStore &ls) override
    {
        ls.RawIOHandle(pret, Target);
        return 1;
    }
};


ObjectMethodsTable::Register<AddItemMethod<Platform::CoreType, &Platform::AddCoreType>> 
    REGUID(PlatformIface, "addcoretype");
ObjectMethodsTable::Register<AddItemMethod<Platform::DmaController, &Platform::AddDmaController>> 
    REGUID(PlatformIface, "adddma");


ObjectMethodsTable::Register<AddItemMethod<Platform::Core, &Platform::AddCore>> REGUID(PlatformIface, "addcore");
ObjectMethodsTable::Register<AddItemMethod<Platform::Memory, &Platform::AddMemory>> REGUID(PlatformIface, "addmem");


class AddLinkMethod : public MethodInterface<Platform>
{
    Platform::Core *pCore;
    Platform::Memory *pMem;
    int ReadCost, WriteCost;
    
    virtual bool ReadArgs(LoadStore &ls) override
    {
        return ls.IOHandle("core", pCore, Target)
             & ls.IOHandle("mem", pMem, Target)
             & ls.IO("writecost", WriteCost, true, 0, 0)
             & ls.IO("readcost", ReadCost, true, 0, 0);
    }
    virtual bool Run() override
    {
        Target->AddEdge(pCore, pMem, ReadCost, WriteCost);
        return true;
    }
};
ObjectMethodsTable::Register<AddLinkMethod> REGUID(PlatformIface, "addlink");

class AddDMALinkMethod : public MethodInterface<Platform>
{
    Platform::Memory *pFrom, *pTo;
    std::vector<Platform::DmaController*> Controllers;
    int FixCost, WriteCost;
    
    virtual bool ReadArgs(LoadStore &ls) override
    {
        if(!(ls.IOHandle("from", pFrom, Target)
             & ls.IOHandle("to", pTo, Target)
             & ls.IOHandles("controllers", Controllers, Target)
             & ls.IO("writecost", WriteCost, true, 0, 0)
             & ls.IO("fixcost", FixCost, true, 0, 0))) return false;
        if(Controllers.size() < 1 || Controllers.size() > 2)
        {
            ls.Error("Unsupported number of controllers (must be 1 or 2)");
            return false;
        }
        return true;
    }
    virtual bool Run() override
    {
        Target->AddEdge(pFrom, pTo, FixCost, WriteCost, Controllers);
        return true;
    }
};
ObjectMethodsTable::Register<AddDMALinkMethod> REGUID(PlatformIface, "adddmalink");

class AddGroupMethod : public AddItemMethod<Platform::Group, &Platform::AddGroup>
{
    virtual int WriteReturn(LoadStore &ls) override { return MethodInterfaceBase::WriteReturn(ls); }
};
ObjectMethodsTable::Register<AddGroupMethod> REGUID(PlatformIface, "addgroup");

class GraphvizOutput : public MethodInterface<Platform>
{
    std::string Filename;
    bool ReadArgs(LoadStore & ls) override
    {
        return ls.IO("filename", Filename);
    }
    bool Run() override
    {
        std::ofstream strm(Filename);
        if(!strm.is_open())
        {
            perror(Filename.c_str());
            return false;
        }
        Ladybirds::graph::Dump(Target->GetGraph(), strm,
            [](const Platform::ComponentNode &n) {
                if(n.pCore) return strprintf("label=\"%s\", shape=rectangle", n.pCore->Name.c_str());
                if(n.pMem)  return strprintf("label=\"%s\", shape=polygon,sides=7", n.pMem->Name.c_str());
                return std::string("label=\"?\",shape=star");
            },
            [](const Platform::HwConnection &e) {
                if(e.Controllers.empty()) return strprintf("label=\"r=%d, w=%d\"", e.ReadCost, e.WriteCost);
                else return strprintf("label=\"%d + s*%d\"", e.FixCost, e.WriteCost);
            });
        return true;
    }
};
ObjectMethodsTable::Register<GraphvizOutput> REGUID(PlatformIface, "graphviz");

/// Platform: Pseudo pass creating and returning a new platform
class PlatformPass : public Ladybirds::lua::Pass
{
    using Pass::Pass;
    virtual int Run(lua_State *lua) override
    {
        PlatformIface.CreateMetaTable(lua, Platform::TypeString);
        
        Ladybirds::lua::LuaDump ld(lua);
        ld.CreateManaged<Platform>();
        return 1;
    };
};
static PlatformPass MyPlatformPass("CreatePlatform", nullptr);
