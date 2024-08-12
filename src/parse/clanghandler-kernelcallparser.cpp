// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "clanghandler-kernelcallparser.h"

#include <memory>

#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Stmt.h>

#include "parse/cinterface.h"
#include "packet.h"
#include "program.h"
#include "tools.h"

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

namespace Ladybirds{namespace parse {

bool ClangHandler::KernelCallParser::EvaluateExpr(const clang::Expr * expr, clang::APValue & val)
{
    clang::Expr::EvalResult evalres;
    if(!expr->EvaluateAsRValue(evalres, *ClangHandler_.Context_))
    {
        ClangHandler_.RaiseError(expr, "Cannot evaluate expression: Make sure it is a compile-time constant");
        return false;
    }
    val = evalres.Val;
    return true;
}
    
bool ClangHandler::KernelCallParser::ExtractInteger(const clang::Expr * expr, int & result)
{
    clang::APValue val;
    if(!EvaluateExpr(expr, val)) return false;
    
    if(!val.isInt())
    {
        ClangHandler_.RaiseError(expr, "Expected: Integer");
        return false;
    }
    
    result = val.getInt().getSExtValue();
    return true;
}

void ClangHandler::KernelCallParser::ProcessVariableDeclaration(const clang::ValueDecl *decl)
{
    auto packet = ClangHandler_.ExtractPacket(decl, nullptr);
    packet.SetAccessType(spec::Packet::inout);
    
    Metakernel_.Variables.push_back(std::move(packet));
    Metakernel_.DeclMap.emplace(decl, MetaKernelSeq::Declaration({&Metakernel_.Variables.back(), -1}));
}


void ClangHandler::KernelCallParser::ProcessKernelCall(const clang::CallExpr *callExpr, bool transform,
                                                       bool fromMetakernel)
{
    auto kernelname = callExpr->getDirectCallee()->getNameAsString();
    spec::Kernel * pkernel = ClangHandler_.Program_.Kernels[kernelname];
    
    if(!pkernel)
    {
        ClangHandler_.RaiseError(callExpr, "Cannot invoke '%0': Not a kernel") << kernelname;
        return;
    }

    if(!GenerateOperationFromCall(callExpr, pkernel)) return;
    if(transform) TransformKernelCall(callExpr, Metakernel_.Operations.back(), fromMetakernel);
}

///\internal Given a (supposed) variable access or sub-array access expression,
/// returns the original variable that is accessed
/// and stores the index ranges for a possibly sub-array access in \p indices. May return null on failure.
const clang::DeclRefExpr* 
ClangHandler::KernelCallParser::ExtractArrayAccess(const clang::Expr* expr, gen::Space& indices)
{
    assert(indices.Dimensions() == 0);
    
    while (auto *arraySubscript = dyn_cast<clang::ArraySubscriptExpr>(expr->IgnoreImpCasts()))
    {
        // We are accessing parts of an array
        expr = arraySubscript->getBase();
        auto *binaryOperator = dyn_cast<clang::BinaryOperator>(arraySubscript->getIdx()->IgnoreImpCasts());
        if (binaryOperator && binaryOperator->getOpcode() == clang::BO_Comma)
        {
            if(auto subbin = dyn_cast<clang::BinaryOperator>(binaryOperator->getLHS()))
            {
                if(subbin->getOpcode() == clang::BO_Comma)
                    ClangHandler_.RaiseError(subbin->getRHS(), "Superfluent index");
            }
            
            int first = 0, last = 0;
            bool success = ExtractInteger(binaryOperator->getLHS(), first);
            success = ExtractInteger(binaryOperator->getRHS(), last) && success;
            
            if(first > last)
            {
                if(success) ClangHandler_.RaiseError(binaryOperator->getOperatorLoc(), 
                    "Invalid range: First element (%0) is greater than last (%1)") 
                    << binaryOperator->getSourceRange() << first << last;
                first = last;
            }
            indices.push_back(Range::FirstLast(first, last));
        }
        else
        {
            int singleidx = 0;
            ExtractInteger(arraySubscript->getIdx(), singleidx);
            indices.push_back(Range::FirstLast(singleidx, singleidx));
        }
    }
    std::reverse(indices.begin(), indices.end());
    
    auto *declRef = dyn_cast<clang::DeclRefExpr>(expr->IgnoreImpCasts());
    if (!declRef) ClangHandler_.RaiseError(expr, "Expected: Variable or (sub-)array");
    return declRef;
}


bool ClangHandler::KernelCallParser::GenerateOperationFromCall(const clang::CallExpr *callExpr, Kernel *pkernel)
{
    MetaKernelSeq::KernelCall::ArgVec arguments;
    std::vector<int> parameters;

    bool generateVariables = false;
    if (&Metakernel_ == &ClangHandler_.MainMetakernel_)  //TODO!!!
        generateVariables = true;
    
    int nargs = callExpr->getNumArgs(), nparams = pkernel->Params.size(), npackets = pkernel->Packets.size();
    if(nargs != nparams + npackets)
    {
        ClangHandler_.RaiseError(callExpr, "Wrong number of arguments (expected %0, got %1")
            << (nparams + npackets) << nargs;
        return false;
    }
    
    parameters.reserve(nparams);
    auto *const * clangargs = callExpr->getArgs();
    for(int i = 0, nparams = pkernel->Params.size(); i < nparams; ++i)
    {
        auto * argexpr = *(clangargs++);
        int param;
        if(!ExtractInteger(argexpr, param))
        {
            ClangHandler_.RaiseError(argexpr, "Value for parameter %0 ('%1') could not be determined.")
                << i << pkernel->Params[i].GetName();
            return false;
        }
        parameters.push_back(param);
    }

    for(int i = 0; i < npackets; ++i)
    {
        auto * argexpr = *(clangargs++);
        MetaKernelSeq::KernelCall::Argument::RangeVec indices;
        auto *declRef = ExtractArrayAccess(argexpr, indices);
        
        auto clangdecl = declRef->getDecl();
        
        if(!isa<clang::VarDecl>(clangdecl))
        {
            ClangHandler_.RaiseError(declRef, "Expected data packet, got %0") << declRef->getDecl()->getDeclKindName();
            return false;
        }
        
        if(clangdecl->isDefinedOutsideFunctionOrMethod() && !generateVariables)
        {
            ClangHandler_.RaiseError(declRef, "Global variables are not allowed in meta-kernels");
            return false;
        }

        if(Metakernel_.GenVars.count(declRef->getDecl()) > 0)
        {
            ClangHandler_.RaiseError(declRef, "Expected data package. Generator variables are not allowed here");
            return false;
        }
        

        if (generateVariables)
            ProcessVariableDeclaration(declRef->getDecl());

        auto & decl = Metakernel_.DeclMap[declRef->getDecl()];
        if (!decl.pVar)
        {
            ClangHandler_.RaiseError(declRef, "Internal error: Cannot resolve '%0'") << declRef->getDecl();
            return false;
        }

        arguments.emplace_back(decl.pVar, std::move(indices));

        if (!arguments.back().IsValid())
            ClangHandler_.RaiseError(argexpr, "Invalid argument:\n%0") << arguments.back().GetErrorDesc();
        if(decl.ParentIfaceIndex >= 0) arguments.back().SetBufferHint(decl.ParentIfaceIndex);
    }
    
    // put together a list of "arguments" for a later call to Expr::EvaluateWithSubstitution, such that the derived
    // parameters can later be determined
    std::deque<clang::IntegerLiteral> integerSubstArgs;
    std::deque<clang::CXXNullPtrLiteralExpr> nullSubstArgs;
    std::vector<clang::Expr*> substargs; substargs.reserve(nargs);
    
    //clangargs = callExpr->getArgs();
    auto itargdecls = ClangHandler_.KernelInfo_.at(pkernel).FunDecl->param_begin();
    for(auto paramval : parameters)
    {
        auto type = (*(itargdecls++))->getType();
        llvm::APInt val(ClangHandler_.Context_->getIntWidth(type), paramval, type->isSignedIntegerType());
        integerSubstArgs.emplace_back(*ClangHandler_.Context_, val, type, clang::SourceLocation());
        substargs.push_back(&integerSubstArgs.back());
    }
    for(int i = 0; i < npackets; ++i)
    {
        nullSubstArgs.emplace_back((*(itargdecls++))->getType(), clang::SourceLocation());
        substargs.push_back(&nullSubstArgs.back());
    }

    // calculate derived parameters
    std::vector<int> derivedparams(pkernel->DerivedParams.size());
    auto & ki = ClangHandler_.KernelInfo_.at(pkernel);
    for(auto & exppair : ki.Expressions)
    {
        auto * pexp = exppair.first;
        
        clang::APValue val;
        if(!pexp->EvaluateWithSubstitution(val, *ClangHandler_.Context_, ki.FunDecl, substargs))
        {
            ClangHandler_.RaiseError(pexp, "Cannot evaluate this expression for kernel instantiation");
            ClangHandler_.RaiseError(callExpr, "Instantiation is here");
            return false;
        }
        
        if(!val.isInt())
        {
            ClangHandler_.RaiseError(pexp, "Expression is not an integer (discovered during kernel instantiation)");
            ClangHandler_.RaiseError(callExpr, "Instantiation is here");
            return false;
        }
        
        
        int val_int =  val.getInt().getSExtValue();
        if(val_int <= 0)
        {
            ClangHandler_.RaiseError(pexp, "Expression must be strictly positive, but evaluates to %0 "
                                           "(discovered during kernel instantiation)") << val_int;
            ClangHandler_.RaiseError(callExpr, "Instantiation is here");
            return false;
        }

        derivedparams[exppair.second-1] = val_int;
    }

    Metakernel_.Operations.emplace_back(pkernel, std::move(arguments), std::move(parameters), std::move(derivedparams));

    if (!Metakernel_.Operations.back().IsValid())
    {
        auto & errdesc =  Metakernel_.Operations.back().GetErrorDesc();
        if(!errdesc.empty())
        {
            ClangHandler_.RaiseError(callExpr, "Invalid kernel call:\n%0") << errdesc;
            return false;
        }
    }

    return true;
}


bool ClangHandler::KernelCallParser::TransformKernelCall(const clang::CallExpr *callExpr,
                                                         const MetaKernelSeq::KernelCall & call,
                                                         bool fromMetakernel)
{
    if(!call.IsValid()) return false;
    
    auto & arguments = call.GetArguments();
    auto & params = call.GetParameters();
    assert(arguments.size() + params.size() == callExpr->getNumArgs());
    auto pkernel = call.GetCallee();
    
    if(fromMetakernel && ClangHandler_.Options_.Instrumentation)
    {
        ClangHandler_.Rewriter_.InsertTextBefore(callExpr->getBeginLoc(),
            strprintf("_lb_tinstr.Call%s(\"%s\"), ", pkernel->IsMetaKernel() ? "Meta" : "", pkernel->Name.c_str()));
    }
    
    auto *const* argexprs = callExpr->getArgs() + params.size();
    auto rawsubs = const_cast<clang::CallExpr*>(callExpr)->getRawSubExprs().drop_front(params.size()+1);
    for (size_t i = 0; i < arguments.size(); ++i)
    {
        const auto &relvdims = arguments[i].GetRelevantDims();
        
        const clang::Expr *expr = argexprs[i];
        
        while (auto *arraySubscript = dyn_cast<const clang::ArraySubscriptExpr>(expr->IgnoreImpCasts()))
        {
            // We are accessing parts of an array
            expr = arraySubscript->getBase();
            auto *binop = dyn_cast<clang::BinaryOperator>(arraySubscript->getIdx()->IgnoreImpCasts());
            if (binop && binop->getOpcode() == clang::BO_Comma)
            {
                ClangHandler_.Rewriter_.RemoveText(clang::SourceRange(binop->getOperatorLoc(), binop->getEndLoc()));
            }
        }
        
        auto *declRef = cast<const clang::DeclRefExpr>(expr->IgnoreImpCasts());
        //error checks have already beed made in GenerateOperationFromCall
        
        auto & var = *arguments[i].GetVariable();
        int length =  var.GetArrayDims().size();
        
        std::string dimstring = ClangHandler_.Options_.Instrumentation ? ", {" : "(int []){";
        bool passingArgument = isa<clang::ParmVarDecl>(declRef->getDecl());
        if(passingArgument && fromMetakernel)
        {
            std::string dimspec = ClangHandler_.Options_.Instrumentation ? var.GetName() + ".size()["
                                                                           : "_lb_size_" + var.GetName() + "[";
            auto dimspecend = dimspec.length();
            
            int lastidx = length+1;
            for (int n = relvdims.size(); n-- > 0; )
            {
                int idx = relvdims[n]+1;
                for(auto dsidx = idx; dsidx < lastidx; ++dsidx)
                {
                    (dimspec.erase(dimspecend) += std::to_string(length-dsidx)) += "]*";
                    dimstring += dimspec;
                }
                dimstring.pop_back();
                if(n > 0) dimstring += ", ";
                lastidx = idx;
            }
        }
        else
        {
            auto itdimsbase = var.GetArrayDims().begin();
            
            int lastidx = length;
            for (auto n = relvdims.size(); n-- > 0; )
            {
                int idx = relvdims[n]+1;
                int sz = Product(itdimsbase+idx, itdimsbase+lastidx);
                if(n+1 == relvdims.size() && sz != 1)
                {
                    ClangHandler_.RaiseError(declRef, "Sorry, collapsing at array end is not supported yet."); //TODO
                }
                dimstring += std::to_string(sz);
                if(n > 0) dimstring += ", ";
                lastidx = idx;
            }
        }
        
        auto pargstmt = rawsubs[i];
        if(ClangHandler_.Options_.Instrumentation)
        {
            ClangHandler_.Rewriter_.InsertTextBefore(pargstmt->getBeginLoc(),
                strprintf("{\"%s\", %s", pkernel->Packets[i].GetName().c_str(),
                          passingArgument || argexprs[i]->getType()->isAnyPointerType() ? "" : "&"));
            dimstring += "}}";
            //ClangHandler_.Rewriter_.ReplaceText(expr->getSourceRange(), "test");
            ClangHandler_.Rewriter_.InsertTextAfterToken(pargstmt->getEndLoc(), dimstring);
        }
        else
        {
            dimstring +=  "}, ";
            if(!argexprs[i]->getType()->isAnyPointerType())
                dimstring += '&';
        
            ClangHandler_.Rewriter_.InsertTextBefore(pargstmt->getBeginLoc(), dimstring);
        }
    }
    
    return true;
}

}} //namespace Ladybirds::parse
