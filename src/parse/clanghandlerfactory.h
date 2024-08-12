// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef CLANGHANDLERFACTORY_H
#define CLANGHANDLERFACTORY_H

#include <memory>

// Seperate header to minimize dependencies

namespace clang { class ASTConsumer; }
namespace Ladybirds { namespace impl { struct Program; }}


namespace Ladybirds { namespace parse {

struct CSpecOptions;

//! Creates ClangHandler objects. For use with clang::tooling::newFrontendActionFactory
class ClangHandlerFactory
{
private:
    CSpecOptions & Opts_;
    impl::Program & ProgRef_;
public:
    inline ClangHandlerFactory(CSpecOptions & opt, impl::Program & progref) : Opts_(opt), ProgRef_(progref) {}
    std::unique_ptr<clang::ASTConsumer> newASTConsumer(); //implemented in clanghandler.cpp
};


}} //namespace Ladybirds::parse

#endif // CLANGHANDLERFACTORY_H
