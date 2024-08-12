// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef CINTERFACE_H
#define CINTERFACE_H

#include <vector>
#include <string>

namespace Ladybirds {

namespace impl {struct Program; }

namespace parse {

struct CSpecOptions
{
    enum PacketDeclTransformKind ///< how should a packet declaration in a meta-kernel be translated to C?
    {
        None, ///< Leave as is (which effectively means stack allocation)
        Malloc, ///< Allocate the shelves on the heap using malloc
        Output ///< Add the packets as additional (unconnected) outputs to the metakernel declaration
    };
    
    std::string SpecificationFile; ///< Input .lb file
    std::string TranslationOutput; ///< empty means no output
    bool OnlyParse = false; ///< If true, don't load the parsed program into the internal representation
    bool Instrumentation = false; ///< If true, inject instrumentation code for counting packet accesses
    PacketDeclTransformKind PacketDeclTransform = None;
    
    CSpecOptions() = default;
    CSpecOptions(std::string specfile) ///< Simple constructor for the non-backend translation
     : SpecificationFile(std::move(specfile)),
       TranslationOutput(SpecificationFile+".c"),
       OnlyParse(true), 
       PacketDeclTransform(Malloc)
    {}
};

/// Parses a Ladybirds C specification from a .lb file and rewrites it to C code, producing a .lb.c file.
/// If \p onlyparse is set, that was it; otherwise also loads the parsed program into the internal representation \p prog.
bool LoadCSpec(CSpecOptions &options, impl::Program &prog);

}} //namespace Ladybirds::parse

#endif // CINTERFACE_H
