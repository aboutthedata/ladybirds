// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef BASETYPE_H
#define BASETYPE_H

#include <string>

namespace Ladybirds {
namespace spec {

//! Represents a base type (such as int, char, ...) that can be used to build e.g. arrays
class BaseType
{
public:
    std::string Name;
    int Size;
public:
    inline BaseType(std::string name, int size) : Name(std::move(name)), Size(size) {}
    
    BaseType ( const BaseType &other ) = delete; //disabled by default
    BaseType &operator= ( const BaseType &other ) = delete; //disabled by default
    bool operator== ( const BaseType &other ) const = delete; //disabled by default
    
    //! Returns true if the given type is binary compatible to this one (such as unsigned int and int)
    bool IsCompatible(const BaseType & bt) const {return bt.Size == Size;} //no big fuss for now; might change in the future
    
    static const BaseType * FromString(const std::string & name, bool * success = nullptr);
};



} //namespace spec
} //namespace Ladybirds

#endif // BASETYPE_H
