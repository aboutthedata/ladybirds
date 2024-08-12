// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef CLANGHANDLER_KERNELCALLPARSER_H
#define CLANGHANDLER_KERNELCALLPARSER_H

#include "clanghandler.h"
namespace Ladybirds{namespace parse {

class ClangHandler::KernelCallParser
{
protected:
    ClangHandler & ClangHandler_;
    MetaKernelSeq & Metakernel_;
    
public:
    inline KernelCallParser(ClangHandler & ch, MetaKernelSeq & mks)
        : ClangHandler_(ch), Metakernel_(mks) {}
    virtual ~KernelCallParser() {}
    
    virtual bool EvaluateExpr(const clang::Expr * expr, clang::APValue & val);
    bool ExtractInteger(const clang::Expr * expr, int & result);
    const clang::DeclRefExpr* ExtractArrayAccess(const clang::Expr* expr, gen::Space& indices);
    
    void ProcessKernelCall(const clang::CallExpr *callExpr, bool transform = true, bool FromMetakernel = true);
    bool GenerateOperationFromCall(const clang::CallExpr *callExpr, spec::Kernel *kernel);
    bool TransformKernelCall(const clang::CallExpr *callExpr, const MetaKernelSeq::KernelCall &call,
                             bool FromMetakernel);
    
    void ProcessVariableDeclaration(const clang::ValueDecl *decl);
};

}} //namespace Ladybirds::parse

#endif // CLANGHANDLER_KERNELCALLPARSER_H
