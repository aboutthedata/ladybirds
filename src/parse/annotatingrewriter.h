// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_PARSE_ANNOTATINGREWRITER_H
#define LADYBIRDS_PARSE_ANNOTATINGREWRITER_H

#include <clang/Rewrite/Core/Rewriter.h>

namespace llvm { class raw_ostream; }

namespace Ladybirds {
namespace parse {

/// An extension to clang::Rewriter that provides the method WriteWithAnnotations.
class AnnotatingRewriter : public clang::Rewriter
{
public:
    /** Writes the rewritten file identified by \p fid to the stream \p strm (like GetRewriteBufferFor(...).write(...)).
     *  However, where necessary, C code precompiler directives ("#line") are inserted that provide hints about the
     *  correspondence between line numbers in the output and the original source file.
     **/
    llvm::raw_ostream &WriteWithAnnotations(clang::FileID fid, llvm::raw_ostream &strm) const;
};

    
}} //namespace Ladybirds::parse

#endif // LADYBIRDS_PARSE_ANNOTATINGREWRITER_H
