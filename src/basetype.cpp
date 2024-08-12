// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "basetype.h"

#include <memory>
#include <unordered_map>

#include "msgui.h"


using namespace std;

namespace Ladybirds { namespace spec {

//Ideally, we should have this list as a member of Program.
//However, we then would need to pass it somehow to Packet::LoadStoreMembers, and that makes it complicated
const BaseType * BaseType::FromString(const std::string &name, bool * success)
{
    static class TypeMap : public  std::unordered_map<std::string, std::unique_ptr<BaseType>>
    {public:
        TypeMap()
        {
            static constexpr struct {const char * Name; int Size;} types[] = 
                {{"char", 1}, {"int", 4}, {"long", 8}, {"float", 4}, {"double", 8},
                {"int8_t", 1}, {"int16_t", 2}, {"int32_t", 4}, {"int64_t", 8}, {"int128_t", 16},
                {"uint8_t", 1}, {"uint16_t", 2}, {"uint32_t", 4}, {"uint64_t", 8}, {"uint128_t", 16}};
            reserve(sizeof(types)/sizeof(*types));
            for(auto & t : types)
            {
                emplace(t.Name, make_unique<BaseType>(t.Name, t.Size));
            }
        }
    } typemap;
    
    auto it = typemap.find(name);
    if(it != typemap.end()) return it->second.get();
    
    if(success)
    {
        *success = false;
        gMsgUI.Error("Unknown data type: %s. Assuming size 1.", name.c_str());
    }
    else gMsgUI.Warning("Unknown data type: %s. Assuming size 1.", name.c_str());
    
    auto ret = typemap.emplace(name, make_unique<BaseType>(name, 1));
    return ret.first->second.get();
}




}}//namespace Ladybirds::spec