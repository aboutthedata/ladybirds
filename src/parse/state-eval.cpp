// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

// WORKAROUND: Since Clang does not provide an interface to its statement evaluation engine,
// we need to replicate it here (ExprConstant.cpp, Interp/*.h are copied directly from the LLVM github repository).
// We have to include the CPP file here to be able to call its internal functions (which are declared as static).
// Then we use it to implement our interface.
#include "clang-code/ExprConstant.cpp"

#include "state-eval.h"

#include <deque>

using namespace clang;

namespace clang_ext {

class EvalState::Impl {
public:
  Expr::EvalStatus Status;
  EvalInfo Info;
  CallStackFrame Frame;
  std::deque<BlockScopeRAII> Scopes;
  const ASTContext &Context_;

  Impl(const ASTContext &Ctx, const FunctionDecl *pFn) :
       Info(Ctx, Status, EvalInfo::EM_ConstantExpressionUnevaluated),
       Frame(Info, clang::SourceLocation(), pFn, /*This*/nullptr, CallRef()),
       Context_(Ctx)
       {}
};

EvalState::EvalState(ASTContext & Ctx, const FunctionDecl *pFn) : pImpl(new Impl(Ctx, pFn)) {}
EvalState::~EvalState() { delete pImpl; }

void EvalState::StartBlock()
{
  pImpl->Scopes.emplace_back(pImpl->Info);
}

void EvalState::EndBlock()
{
  pImpl->Scopes.pop_back();
}

bool EvalState::Evaluate(const Stmt *stmt, APValue &Value)
{
    StmtResult sr = {Value, nullptr};
    return (EvaluateStmt(sr, pImpl->Info, stmt, nullptr) == ESR_Succeeded);
}

bool EvalState::Evaluate(const Expr *expr, APValue &Value)
{
    return EvaluateAsRValue(pImpl->Info, expr, Value);
}

} // namespace clang_ext

