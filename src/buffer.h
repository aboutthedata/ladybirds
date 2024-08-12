// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LORRY_H
#define LORRY_H

#include "graph/presdeque.h"
#include "loadstore.h"

namespace Ladybirds { namespace spec { class Packet; }}

namespace Ladybirds {  namespace impl {

//!Class for representing a buffer (i.e. a memory block used to "carry" data packets)
class Buffer : public graph::PresDequeElementBase, public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE("Buffer");
    
public:
    int Size = 1;        //!< Size, in bytes, of the buffer
    int MemBank = 0;    //!< Memory bank to store the buffer in (used for optimizing on MPPA)
    int BankOffset = -1; //!< Memory address of the buffer within the bank (i.e. 0 means at the start of the bank)
    
    const spec::Packet *pExternalSource = nullptr; ///< For packets provided from outside to a metakernel on invocation
    
public:
    inline Buffer() {}
    explicit Buffer ( const Buffer &other ) = default;  // want to "copy" buffers e.g. during buffer merging
    Buffer &operator= ( const Buffer &other ) = delete; //dito

    virtual bool LoadStoreMembers(loadstore::LoadStore &ls) override;
};

}} //namespace Ladybirds::impl

#endif // LORRY_H
