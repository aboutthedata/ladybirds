// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef CLANGHANDLER_H_
#define CLANGHANDLER_H_

#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

#include <clang/AST/DeclBase.h>
#include <clang/AST/Expr.h>
#include <clang/Frontend/FrontendAction.h>

#include "parse/annotatingrewriter.h"
#include "parse/exprcmp.h"
#include "metakernelseq.h"
#include "packet.h"

namespace Ladybirds{
    
namespace impl { struct Program; }
namespace spec { class Kernel; }

namespace parse {

struct CSpecOptions;
    
//! Class for the actual AST information extraction and code transformation work
class ClangHandler : public clang::ASTConsumer
{
private:
    struct KernelExpressions
    {
        const clang::FunctionDecl * FunDecl;
        std::set<const clang::Decl*> Params;
        std::map<const clang::Expr*, int, ExprCmp> Expressions;
        inline KernelExpressions(const clang::FunctionDecl * fundecl, clang::ASTContext *pctx)
            : FunDecl(fundecl), Expressions(ExprCmp(*pctx)) {}
    };
    struct DimExprVisitor;
    
    using Program = impl::Program;
    CSpecOptions &Options_;
    Program &Program_;
    clang::ASTContext * Context_;
    AnnotatingRewriter Rewriter_;
    
    MetaKernelSeq MainMetakernel_;

    std::unordered_map<spec::Kernel *, KernelExpressions> KernelInfo_;

public:
    ClangHandler(CSpecOptions & opts, Program &program) : Options_(opts), Program_(program), MainMetakernel_(nullptr) {};
    ClangHandler(const ClangHandler &) = delete; //by default
    ClangHandler &operator=(const ClangHandler &) = delete; //dito

    //! Reads and transforms one Ladybirds C specification file
    virtual void HandleTranslationUnit(clang::ASTContext& ctx) override;

    //! Prints out an error using the clang diag machinery. Mainly for internal use.
    template<unsigned N> auto RaiseDiag(clang::DiagnosticsEngine::Level level, clang::SourceLocation loc,
                                        const char (&message)[N]);
    template<unsigned N> auto RaiseError(clang::SourceLocation loc, const char (&message)[N]);
    template<unsigned N> auto RaiseError(const clang::Stmt * stmt, const char (&message)[N]);
private:
    //! Parses the given Ladybirds C specification file. Errors are communicated through the Clang diag interface
    void Parse();
    //! Writes the transformed code to \p filename. Errors are communicated through the Clang diag interface
    void WriteTransformedCode() const;
    
    
    clang::QualType ExtractArrayType(clang::QualType type, std::vector< int, std::allocator< int > >& dims,
                                     KernelExpressions* pke, const clang::SourceRange & diagRange);
    spec::Packet ExtractPacket(const clang::ValueDecl* decl, KernelExpressions* pke);
    
    
    void ProcessKernelFunction(const clang::FunctionDecl *functionDecl);
    bool TransformKernelDecl(const clang::FunctionDecl *functionDecl, spec::Kernel *pkernel);
    bool GenerateKernelFromFunctionDecl(const clang::FunctionDecl *functionDecl, spec::Kernel *kernel );

    void ProcessInvoke(const clang::CallExpr * callExpr);
    void ProcessMetakernelBody(const clang::FunctionDecl& kernelDecl, MetaKernelSeq& kernelSeq);
    bool TransformKernelCall(const clang::CallExpr *callExpr, clang::SourceLocation tmpInsert, const MetaKernelSeq::KernelCall &call);
    bool GenerateOperationFromCall(const clang::CallExpr *callExpr, MetaKernelSeq *metakernel, spec::Kernel *kernel);
    void UpdateSourceProperty(const clang::FunctionDecl& kernelDecl, spec::Kernel& kernel);

    //! \internal Internal class for handling matcher callbacks using lambdas
    class MatchLambda;

    //! \internal Internal class for parsing and rewriting call statements to kernels
    class KernelCallParser;

    //! \internal Internal class for visiting all statements in a metakernel
    class MetakernelVisitor;
};

template<unsigned N>
auto ClangHandler::RaiseDiag(clang::DiagnosticsEngine::Level level, clang::SourceLocation loc, const char (&message)[N])
{
    auto & diag = Context_->getDiagnostics();
    auto msgid = diag.getCustomDiagID(level, message);
    return diag.Report(loc, msgid);
}

template<unsigned N>
auto ClangHandler::RaiseError(clang::SourceLocation loc, const char (&message)[N] )
{
    return RaiseDiag(clang::DiagnosticsEngine::Error, loc, message);
}

template<unsigned N>
auto ClangHandler::RaiseError(const clang::Stmt * stmt, const char (&message)[N])
{
    return RaiseError(stmt->getBeginLoc(), message) << stmt->getSourceRange();
}


}} //namespace Ladybirds::parse

#endif /* CLANGHANDLER_H_ */
