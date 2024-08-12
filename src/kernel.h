// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef KERNEL_H
#define KERNEL_H

#include <string>
#include <vector>

#include "loadstore.h"
#include "packet.h"

namespace Ladybirds { namespace spec {

//!Class for representing a kernel (i.e. a function with a certain amount of work)
class Kernel : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(Kernel)
public:
    Kernel() = default;
    Kernel(Kernel &&) = default;
    Kernel & operator= (Kernel &&) = default;
protected:
    Kernel(const Kernel &);                      //Necessary for MetaKernel copy constructor, therefore only protected
    Kernel & operator= (const Kernel &) = delete;//by default
    
public:
    //! Name of the kernel
    std::string Name;
    //! Name of the C function that implements the kernel
    std::string FunctionName;
    //! Name of the C code file that contains kernel implementation
    std::string CodeFile;
    //! Source code of the kernel
    std::string SourceCode;
    //! The data packets (i.e. the arguments and return values) of the kernel
    std::vector<Packet> Packets;
    //! The parameters of the kernel
    std::vector<Packet> Params;
    //! The formulae for the derived parameters (i.e. numbers that can be calculated from the parameters) of the kernel
    std::vector<std::string> DerivedParams;
    
    //! Is this kernel object actually a meta-kernel?
    virtual bool IsMetaKernel() const { return false; }
    
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
    
    Packet * PacketByName(const std::string & name);
    inline const Packet* PacketByName(const std::string & name) const
        {return const_cast<Kernel*>(this)->PacketByName(name);}
};

}}//namespace Ladybirds::spec


#endif // KERNEL_H
