// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "packet.h"

#include <memory>

#include "tools.h"

namespace Ladybirds { namespace spec {


const char* Packet::AccessTypeNames[3] = {"in", "out", "inout"};

Packet::Packet(std::string name, AccessType access, const BaseType *type, ArrayDimVector arraydims)
    : Name_(name), Access_(access), BaseType_(type), ArrayDims_(std::move(arraydims))
{
    ComputeSizeof();
}

bool Packet::AddBuddy(Packet *newbuddy)
{
    auto result = Buddies_.insert(newbuddy);
    if(!result.second) return false;
    
    result = newbuddy->Buddies_.insert(this);
    assert(result.second);
    return true;
}



bool Packet::LoadStoreMembers(loadstore::LoadStore& ls)
{
    std::string btname;
    int btsize = 0;
    std::string paramstring;
    if(ls.IsStoring())
    {
        paramstring = strprintf("const int _lb_size_%s[%d], %svoid * _lb_base_%s", Name_.c_str(), 
                                std::max((int) ArrayDims_.size(), 1), Access_ == in ? "const " : "", Name_.c_str());
        btname = BaseType_->Name;
        btsize = BaseType_->Size;
    }
    
    loadstore::EnumStringInterface<AccessType> access(Access_);
    if(!( ls.IO("name", Name_)
        & ls.IO("dir", access)
        & ls.IO("basetype", btname)
        & ls.IO("arraydims", ArrayDims_, false, /*min=*/1)
        & ls.IO("basetypesize", btsize, false, /*min=*/0)
        & ls.IO("paramstring", paramstring, false))) return false;
    
    if(ls.IsLoading())
    {
        BaseType_ = BaseType::FromString(btname);
        ComputeSizeof();
        if(btsize != 0 && btsize != BaseType_->Size) ls.Error("basetypesize is not consistent with internal database");
    }
    
    return true;
}

std::string Packet::GetFullDeclaration() const
{
    return BaseType_->Name + ' ' + Name_ + IndexString(ArrayDims_);
}



void Packet::ComputeSizeof()
{
    NumBytes_ = Product(ArrayDims_) * BaseType_->Size;
}


}}//namespace Ladybirds::spec
