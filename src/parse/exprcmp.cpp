// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "parse/exprcmp.h"

#include <cassert>

#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>

using clang::cast;
using clang::Expr;
using clang::Stmt;

namespace Ladybirds { namespace parse {

static inline int ptrcmp(const void *p1, const void *p2)
{
    if(p1 < p2) return -1;
    if(p1 > p2) return 1;
    return 0;
}

bool ExprCmp::NumericallyIdentical(const clang::Expr *pexpr1, const clang::Expr *pexpr2) const
{
    assert(pexpr1 && pexpr2);
    
    clang::Expr::EvalResult res1, res2;
    if(!pexpr1->EvaluateAsRValue(res1, Ctx_)) return false;
    if(!pexpr2->EvaluateAsRValue(res2, Ctx_)) return false;
    
    if(res1.Val.getKind() != res2.Val.getKind()) return false;
    
    auto v1 = res1.Val, v2 = res2.Val;
    switch(v1.getKind())
    {
        case clang::APValue::Int:          return (v1.getInt() == v2.getInt());
        case clang::APValue::ComplexInt:   return (v1.getComplexIntReal() == v2.getComplexIntReal() &&
                                                   v1.getComplexIntImag() == v2.getComplexIntImag());
        case clang::APValue::Float:        return v1.getFloat().bitwiseIsEqual(v2.getFloat());
        case clang::APValue::ComplexFloat: return (v1.getComplexFloatReal().bitwiseIsEqual(v2.getComplexFloatReal()) &&
                                                   v1.getComplexFloatImag().bitwiseIsEqual(v2.getComplexFloatImag()));
        default: return false;
    }
}

/** 
 *  Adapted from clang: isIdenticalStmt in IdenticalExprChecker.cpp.
 *  
 *  Limitations: (t + u) and (u + t) are not considered identical.
 *  t*(u + t) and t*u + t*t are not considered identical.
 **/
int ExprCmp::Compare(const clang::Expr *pexpr1, const clang::Expr *pexpr2) const
{
    assert(pexpr1 && pexpr2);

    pexpr1 = pexpr1->IgnoreParenImpCasts();
    pexpr2 = pexpr2->IgnoreParenImpCasts();
    
    // If pexpr1 & pexpr2 are of different class then they are not identical statements
    if(int classdiff = pexpr1->getStmtClass() - pexpr2->getStmtClass())
    {
        //...unless they can be evaluated to the same constant result
        if(NumericallyIdentical(pexpr1, pexpr2)) return 0;
        else return classdiff;
    }

    // Compare the children of the expressions
    auto it1 = pexpr1->child_begin(), it1end = pexpr1->child_end(), 
         it2 = pexpr2->child_begin(), it2end = pexpr2->child_end();
    for(; it1 != it1end && it2 != it2end; ++it1, ++it2)
    {
        auto psubexpr1 = static_cast<const clang::Expr*>(*it1), psubexpr2 = static_cast<const clang::Expr*>(*it2);
        if (!psubexpr1 || !psubexpr2)
        {
            if(auto diff = ptrcmp(psubexpr1, psubexpr2)) return diff;
            else continue;
        }
        if(auto diff = Compare(psubexpr1, psubexpr2)) return diff;
    }
    
    // If there are different numbers of children in the statements, they are not the same
    if (it1 != it1end) return -1;
    if (it2 != it2end) return +1;

    //Special directives for certain classes of expressions
    switch (pexpr1->getStmtClass())
    {
        default:
            assert(false); //all important cases should have been handled
            return -1;
        case Stmt::ImplicitCastExprClass:
        case Stmt::ParenExprClass:
            assert(false); //should have been removed before in the beginning
            return 0; //still return 0 because the children have been checked
        case Stmt::ArraySubscriptExprClass:
        case Stmt::CallExprClass:
        case Stmt::OMPArraySectionExprClass:
            return 0; //all necessary checks have already been performed when comparing the children
        case Stmt::BinaryOperatorClass:
        {
            auto *BinOp1 = cast<clang::BinaryOperator>(pexpr1);
            auto *BinOp2 = cast<clang::BinaryOperator>(pexpr2);
            return BinOp1->getOpcode() - BinOp2->getOpcode();
        }
        case Stmt::CStyleCastExprClass:
        {
            auto* CastExpr1 = cast<clang::CStyleCastExpr>(pexpr1);
            auto* CastExpr2 = cast<clang::CStyleCastExpr>(pexpr2);

            return ptrcmp(CastExpr1->getTypeAsWritten().getCanonicalType().getTypePtr(),
                          CastExpr2->getTypeAsWritten().getCanonicalType().getTypePtr());
        }
        case Stmt::CompoundAssignOperatorClass:

        case Stmt::CharacterLiteralClass:
        {
            auto *CharLit1 = cast<clang::CharacterLiteral>(pexpr1);
            auto *CharLit2 = cast<clang::CharacterLiteral>(pexpr2);
            return CharLit1->getValue() - CharLit2->getValue();
        }
        case Stmt::DeclRefExprClass:
        {
            auto *DeclRef1 = cast<clang::DeclRefExpr>(pexpr1);
            auto *DeclRef2 = cast<clang::DeclRefExpr>(pexpr2);
            return ptrcmp(DeclRef1->getDecl(), DeclRef2->getDecl());
        }
        case Stmt::FloatingLiteralClass:
        {
            auto *FloatLit1 = cast<clang::FloatingLiteral>(pexpr1);
            auto *FloatLit2 = cast<clang::FloatingLiteral>(pexpr2);
            switch(FloatLit1->getValue().compare(FloatLit2->getValue()))
            {
                case llvm::APFloat::cmpUnordered:   return 0; //both are NaN
                case llvm::APFloat::cmpEqual:       return 0;
                case llvm::APFloat::cmpLessThan:    return -1;
                case llvm::APFloat::cmpGreaterThan: return 1;
            }
        }
        case Stmt::IntegerLiteralClass:
        {
            auto *IntLit1 = cast<clang::IntegerLiteral>(pexpr1);
            auto *IntLit2 = cast<clang::IntegerLiteral>(pexpr2);

            llvm::APInt I1 = IntLit1->getValue();
            llvm::APInt I2 = IntLit2->getValue();
            if(auto diff = I1.getBitWidth() - I2.getBitWidth()) return diff;
            if(I1 == I2) return 0;
            if(I1.slt(I2)) return -1;
            if(I1.sgt(I2)) return 1;
            assert(false);
        }
        case Stmt::StringLiteralClass:
        {
            auto *StringLit1 = cast<clang::StringLiteral>(pexpr1);
            auto *StringLit2 = cast<clang::StringLiteral>(pexpr2);
            return StringLit1->getBytes().compare(StringLit2->getBytes());
        }
        case Stmt::UnaryOperatorClass:
        {
            auto *UnaryOp1 = cast<clang::UnaryOperator>(pexpr1);
            auto *UnaryOp2 = cast<clang::UnaryOperator>(pexpr2);
            return UnaryOp1->getOpcode() - UnaryOp2->getOpcode();
        }
    }
}

}} //namespace Ladybirds::parse
