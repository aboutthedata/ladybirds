// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef DEPENDENCY_H
#define DEPENDENCY_H

#include <memory>
#include <vector>

#include "range.h"
#include "loadstore.h"

namespace Ladybirds { namespace spec {
    class Iface;

class Dependency : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE("Dependency");
public:
    class Anchor : public LoadStorableCompound
    {
    public:
        Iface * TheIface = nullptr;
        gen::Space Index;
        
        Anchor() = default;
        Anchor(Iface * piface, gen::Space index) : TheIface(piface), Index(std::move(index)) {}
        
        std::string GetFullId() const;
        
        //! \brief Calculates the byte offset between the lowest address of the entire packet and 
        //! the lowest address that is accessed in this connection point.
        int CalcByteOffset() const;
        
        virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
    };
    
public:
    Anchor From;
    Anchor To;
    
    Dependency() = default;
    Dependency(Anchor from, Anchor to) : From(std::move(from)), To(std::move(to)) {}
    
    //! Returns true if From and To together with the corresponing Indices are type compatible.
    bool CheckCompatibility() const;
    long GetMemSize() const; //!< Amount of memory that needs to be transmitted
    
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
    
};

std::ostream & operator <<(std::ostream & strm, Dependency::Anchor & anch);
std::ostream & operator <<(std::ostream & strm, Dependency & dep);


}} //namespaec Ladybirds::spec

#endif // DEPENDENCY_H
