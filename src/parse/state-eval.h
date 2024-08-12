// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef STATEFULEVAL_H_
#define STATEFULEVAL_H_

#include "clang/AST/APValue.h"

namespace clang
{
    class ASTContext;
    class Stmt;
    class Expr;
    class FunctionDecl;
}

namespace clang_ext {
/// State for evaluating a series of statements and expressions.
class EvalState
{
    class Impl;
    Impl * pImpl = nullptr;

public:
    EvalState (clang::ASTContext &Ctx, const clang::FunctionDecl *pFn);
    ~EvalState();
    EvalState (const EvalState &) = delete;
    EvalState ( EvalState &&) = default;
    EvalState & operator=(const EvalState &) = delete;
    EvalState & operator=( EvalState &&) = default;

    /// Evaluate an expression based on the information in this state object
    /// and save the side effects like variable assigment etc. in it.
    /// Differs from Evaluate(Stmt&) in that the result of the expression is stored in \p Value.
    bool Evaluate(const clang::Expr *expr, clang::APValue &Value);

    /// EvaluateWithState - Evaluate a statement based on the information in this state object
    /// and save the side effects like variable assigment etc. in State
    bool Evaluate(const clang::Stmt *stmt, clang::APValue &Value);

    void StartBlock();
    void EndBlock();
};

} //namespace clang_ext

#endif // STATEFULEVAL_H_
