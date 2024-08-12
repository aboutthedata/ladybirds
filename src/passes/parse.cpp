// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <string>

#include "lua/luadump.h"
#include "lua/luaenv.h"
#include "lua/pass.h"
#include "parse/cinterface.h"
#include "program.h"


using std::string;
using Ladybirds::impl::Program;
using Ladybirds::lua::Pass;
using Ladybirds::parse::CSpecOptions;

namespace Ladybirds { namespace loadstore { namespace open {
    static constexpr EnumOptionsList<CSpecOptions::PacketDeclTransformKind, 3> mypackdecllist = { {
        { "none", CSpecOptions::None },
        { "malloc", CSpecOptions::Malloc },
        { "output", CSpecOptions::Output }
    } };
    
    template<>
    struct EnumOptions<CSpecOptions::PacketDeclTransformKind>
    {
        static constexpr auto & list = mypackdecllist;
    };
}}}//namespace Ladybirds::loadstore::open
struct ParseArgs : public Ladybirds::loadstore::LoadStorableCompound, public Ladybirds::parse::CSpecOptions
{
    virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override
    {
        Ladybirds::loadstore::EnumStringInterface<CSpecOptions::PacketDeclTransformKind> packtrans(PacketDeclTransform);
        return ls.IO("filename", SpecificationFile)
             & ls.IO("output", TranslationOutput, false)
             & ls.IO("packetdecltransform", packtrans, false, "none");
    }
};
bool LoadProjectInfo(Ladybirds::impl::Program &prog, ParseArgs  args);

/// Parsing pass: Parses a .lb file, the path to which is provided as an argument, and returns a program object
class ParsePass : public Pass
{
    using Pass::Pass;
    virtual int Run(lua_State * lua) override;
};
static ParsePass MyPass("Parse", nullptr);

int ParsePass::Run(lua_State * lua)
{
    ParseArgs args;
    LoadExtraArgs(lua, args);
    lua_settop(lua, 0); // we have all arguments now
    

    Ladybirds::lua::LuaDump ld(lua);
    auto *pprog = ld.CreateManaged<Program>();
    
    // Finally parse the .lb file
    if(!LoadCSpec(args, *pprog)) return 0;
    
    std::cout << pprog->GetTasks().size() << " tasks, " << pprog->Dependencies.size() << " dependencies" << std::endl;
    return 1;
}

