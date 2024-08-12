// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef PACKET_H
#define PACKET_H

#include <array>
#include <string>
#include <unordered_set>

#include "basetype.h"
#include "loadstore.h"


namespace Ladybirds {namespace spec {

class Kernel;

//!Class for representing a data packet (input, output... arguments) of a kernel.
class Packet : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(Packet);
    friend class Kernel;

public:
    using BaseType = Ladybirds::spec::BaseType;
    using ArrayDimVector = std::vector<int>;
    using BuddyList = std::unordered_set<const Packet*>;
    enum AccessType {invalid= -1, in = 0, out, inout, param};
    static const char * AccessTypeNames[3];
    
private:
    std::string Name_;
    AccessType Access_ = invalid;
    const BaseType* BaseType_ = nullptr;
    ArrayDimVector ArrayDims_;
    Kernel * Kernel_ = nullptr;
    int NumBytes_ = -1;
    BuddyList Buddies_;

public:
    //! Name of the packet
    inline const std::string & GetName() const {return Name_;}
    //! Is it input, output or both?
    inline AccessType GetAccessType() const {return Access_;}
    //! Set the base type of the packet
    inline const BaseType & GetBaseType() const {return *BaseType_;}
    //! Set the dimensions of the array. Empty = no array
    inline const std::vector<int> & GetArrayDims() const {return ArrayDims_;}
    //! The kernel to which the packet belongs
    inline Kernel * GetKernel() {return Kernel_;}
    //! \copydoc GetKernel
    inline const Kernel * GetKernel() const {return Kernel_;}
    //! The buddies of the packet
    inline const BuddyList & GetBuddies() const { return Buddies_; }

    //! Set if it is input, output or both
    inline void SetAccessType(AccessType access) { Access_ = access; }
    //! Adds a packet to the buddies list.
    /** Also adds this packet to \p newbuddy's list. Returns false if the packet was already in the buddy list. **/
    bool AddBuddy(Packet * newbuddy);
    //! Sets the kernel for the packet
    inline void SetKernel(Kernel * pk) { Kernel_ = pk; }
        
    
    Packet() = default;
    Packet(std::string name, AccessType access, const BaseType * type, ArrayDimVector arraydims);
    Packet(const Packet &) = default;              // want to copy kernels
    Packet & operator= (const Packet &) = default; // useful for automatic generation of kernels/tasks
    Packet(Packet &&) = default;
    Packet & operator= (Packet &&) = default;
    
    std::string GetFullDeclaration() const;
    
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
    
private:
    void ComputeSizeof();
};
}//namespace spec

namespace loadstore { namespace open {
	static constexpr EnumOptionsList<spec::Packet::AccessType, 4> mylist = { {
		{ "in", spec::Packet::in },
		{ "out", spec::Packet::out },
		{ "inout", spec::Packet::inout },
		{ "param", spec::Packet::param }
		} };

template<>
struct EnumOptions<spec::Packet::AccessType>
{
	static constexpr auto & list = mylist;
};
}}}//namespace Ladybirds::loadstore::open



#endif // PACKET_H
