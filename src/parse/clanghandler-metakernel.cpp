// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "clanghandler.h"
#include "clanghandler-kernelcallparser.h"

#include <iostream>

#include <memory>
#include <string>
#include <vector>

#include <clang/AST/Attr.h>
#include <clang/AST/StmtVisitor.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Lex/Lexer.h>


#include "state-eval.h"
#include "kernel.h"
#include "metakernelseq.h"
#include "packet.h"
#include "program.h"
#include "range.h"
#include "tools.h"

using std::vector;
using std::string;
using std::make_unique;
using std::to_string;
using std::cerr;
using std::endl;
using clang::ASTContext;
using clang::Stmt;
using clang::FunctionDecl;
using clang::dyn_cast;
using clang::cast;
using clang::isa;
using Ladybirds::spec::Kernel;
using Ladybirds::spec::Packet;
using Ladybirds::gen::Range;
using Ladybirds::gen::Space;

namespace Ladybirds{ namespace parse {


/////////////////
// Handling meta-kernel statements and invokes


static bool IsGenvar(const clang::Decl* decl)
{
    for (const auto *attr : decl->attrs())
    {
        auto *annotateAttr = dyn_cast<const clang::AnnotateAttr>(attr);
        if(annotateAttr && annotateAttr->getAnnotation() == "genvar") return true;
    }
    return false;
}

//! \internal Internal visitor which tests each variable in a given expression on whether it is either
//! global and constant or local and genvar, i.e. usable in a meta-kernel expression to be folded to an integer constant
struct GenvarExprVisitor : public clang::RecursiveASTVisitor<GenvarExprVisitor>
{
private:
    friend class clang::RecursiveASTVisitor<GenvarExprVisitor>;
    ClangHandler & ClangHandler_;
    bool Success_;
    
public:
    explicit GenvarExprVisitor(ClangHandler & ch) : ClangHandler_(ch) {}
    bool Test(const clang::Expr * pexpr)
    {
        Success_ = true;
        TraverseStmt(const_cast<clang::Expr *>(pexpr)); //Unfortunately, RecursiveASTVisitor doesn't support const
        return Success_;
    }
    
private:
    bool VisitDeclRefExpr(clang::DeclRefExpr * pdre)
    {
        auto * pvar = dyn_cast<clang::VarDecl>(pdre->getDecl());
        if(!pvar) return true; //only look at variables, not at enum constants etc.
        
        if(pvar->isLocalVarDecl())
        {
            if(!IsGenvar(pvar))
            {
                ClangHandler_.RaiseError(pdre->getLocation(), 
                    "%0 is not a generator variable") << pdre << pdre->getSourceRange();
                Success_ = false;
            }
        }
        else
        {
            if(!pvar->getType().isConstQualified())
            {
                ClangHandler_.RaiseError(pdre->getLocation(), 
                    "No global, non-const variables are allowed in meta-kernels") << pdre->getSourceRange();
                Success_ = false;
            }
        }
        
        return true;
    }
};

#if 0
std::string decl2str(const clang::Stmt *d, clang::SourceManager &sm) {
    // (T, U) => "T,,"
    string text = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(d->getSourceRange()), sm, clang::LangOptions(), 0);
    if (text.at(text.size()-1) == ',')
        return clang::Lexer::getSourceText(clang::CharSourceRange::getCharRange(d->getSourceRange()), sm, clang::LangOptions(), 0);
    return text;
}
#endif

class ClangHandler::MetakernelVisitor
    : public clang::StmtVisitor<MetakernelVisitor>, public ClangHandler::KernelCallParser
{
private:
    clang_ext::EvalState State_;
    bool Error_ = false;
    int LoopRepeatLevel_ = 0; //Keep track of whether we are repeating evaluation of code
    // Idea: Each loop increases this counter before its second iteration and decreases it after its last iteration

public:
    MetakernelVisitor(ClangHandler & ch, const FunctionDecl *pFn, MetaKernelSeq & mks)
        : KernelCallParser(ch, mks), State_(*ch.Context_, pFn) {}

    void VisitStmt(clang::Stmt *stmt) //fallback for unhandled (i.e. illegal) statement types
    {
        ClangHandler_.RaiseError(stmt, "Illegal statement type (%0) for a meta-kernel") << stmt->getStmtClassName();
                Error_ = true;
    }
    
    void VisitDeclStmt(clang::DeclStmt *declStmt) //variable declaration
    {
        bool first = true, allgenvars, anygenvars = false;
        for(auto pdecl : declStmt->decls())
        {
            auto * pvar = dyn_cast<clang::VarDecl>(pdecl);
            if(!pvar) continue;
            
            if(pvar->isStaticLocal())
            {
                ClangHandler_.RaiseError(declStmt, "Kernels may not contain any static variables")
                    << pvar->getSourceRange();
                                Error_ = true;
            }
            
            bool isgenvar = IsGenvar(pdecl);
            if(first) { allgenvars = isgenvar; first = false; }
            else if(allgenvars != isgenvar)
            {
                ClangHandler_.RaiseError(declStmt, "Cannot mix declaration of data packets and "
                    "generator variables in same statement") << pdecl->getSourceRange();
                                Error_ = true;
                anygenvars = true;
            }
        
            if(isgenvar)
            {
                Metakernel_.GenVars.insert(pvar);
                if(!pvar->hasInit())
                {
                    ClangHandler_.RaiseError(pdecl->getLocation(), "Each generator variable needs an initializer")
                        << pdecl->getSourceRange();
                                        Error_ = true;
                }
            }
            else
            {
                ProcessVariableDeclaration(pvar);
                if(pvar->hasInit())
                {
                    ClangHandler_.RaiseError(pdecl->getLocation(), "Data packets must not have an initializer") 
                        << pvar->getInit()->getSourceRange();
                                        Error_ = true;
                }
            }
        }
        if(allgenvars || anygenvars)
        {
            clang::APValue apval;
            if(!State_.Evaluate(declStmt, apval))
            {
                ClangHandler_.RaiseError(declStmt, "Cannot evaluate statement") << declStmt->getSourceRange();
                                Error_ = true;
            }
        }
    }
    
    void VisitCallExpr(clang::CallExpr *callExpr)
    {
        ProcessKernelCall(callExpr, LoopRepeatLevel_ == 0);
    }
    
    void VisitExpr(clang::Expr * pexpr)
    {
        clang::APValue val;
        EvaluateExpr(pexpr, val);
    }
    
    void VisitCompoundStmt(clang::CompoundStmt * cstmt)
    {
        State_.StartBlock();
        for (Stmt *stmt : cstmt->body()) Visit(stmt);
        State_.EndBlock();
    }

    void VisitForStmt(clang::ForStmt *fs)
    {
        State_.StartBlock();
        
        clang::APValue value;
        if(auto * init = fs->getInit()) Visit(init);
        
        int niterations;
        for(niterations = 0; niterations < 1024; niterations++)
        {
            //Evaluate loop condition
            auto * cond = fs->getCond();
            if(!cond)
            {
                ClangHandler_.RaiseError(fs, "Loops without loop condition are not allowed in meta-kernels");
                                Error_ = true;
                break;
            }
            if(!EvaluateExpr(cond, value)) break;
            if(!value.isInt())
            {
                ClangHandler_.RaiseError(cond, "Loop condition must evaluate to boolean or integer type") 
                    << cond->getSourceRange();
                                Error_ = true;
                break;
            }
            if(value.getInt().getBoolValue() == false) break; //loop is finished!
            
            //Execute loop body
            if(auto * body = fs->getBody())
            {
                                Error_ = false;
                State_.StartBlock();
                Visit(body);
                State_.EndBlock();
                if( Error_ ) break; //do not print the same errors over and over in a loop
            }
            
            //Evaluate increment condition
            if(auto * inc = fs->getInc()) Visit(inc);
            
            if(niterations == 0) ++LoopRepeatLevel_;
        }
        
        if(niterations > 0) --LoopRepeatLevel_;
        State_.EndBlock();
    }

    virtual bool EvaluateExpr(const clang::Expr * expr, clang::APValue & val) override
    {
        //cerr << "Now evaluating expression: " << decl2str(expr, ClangHandler_.Context_->getSourceManager()) << endl;
        bool valid = GenvarExprVisitor(ClangHandler_).Test(expr);
        
        //Still try to evaluate, even if there is an error, to avoid uninitialized generator variables
        if(!State_.Evaluate(expr, val))
        {
            if(valid) //No error message if we already had one before
            {
                ClangHandler_.RaiseError(expr, "Cannot evaluate expression") << expr->getSourceRange();
            }
            return false;
        }
        return true;
    }

};



void ClangHandler::ProcessMetakernelBody(const FunctionDecl& kernelDecl, MetaKernelSeq& kernelSeq)
{
    auto *body = kernelDecl.getBody();
    if (!body)
    {
        RaiseError(kernelDecl.getLocation(), "Missing body for meta-kernel") << kernelDecl.getSourceRange();
        return;
    }

    MetakernelVisitor metakernelVisitor(*this, &kernelDecl, kernelSeq);
    metakernelVisitor.Visit(body);
}




}} //namespace Ladybirds::parse
