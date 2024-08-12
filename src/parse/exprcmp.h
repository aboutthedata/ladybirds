// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_PARSE_EXPRMAP_H
#define LADYBIRDS_PARSE_EXPRMAP_H

namespace clang
{
    class ASTContext;
    class Expr;
}

namespace Ladybirds { namespace parse {

/**
 * \brief Class for comparing two clang Expr objects. Compatible with C++ standard library.
 * The comparison is more or less arbitrary, but consistent such that objects can be sorted.
 */
class ExprCmp
{
private:
    const clang::ASTContext & Ctx_;
    
public:
    inline ExprCmp(const clang::ASTContext & ctx) : Ctx_(ctx) {}
    
    /// Returns true if \p *pexpr1 is considered less than \p *pexpr2. 
    inline bool operator() (const clang::Expr *pexpr1, const clang::Expr *pexpr2) const
        { return (Compare(pexpr1, pexpr2) < 0); }
    
    /// Returns an integer {< 0, == 0, > 0} if \p expr1 {<, ==, >} \p expr2.
    int Compare(const clang::Expr *pexpr1, const clang::Expr *pexpr2) const;
    
    /// Returns true if two expressions can be evaluated to the same numeric constant of same type.
    bool NumericallyIdentical(const clang::Expr *pexpr1, const clang::Expr *pexpr2) const;
};


}} //namespace Ladybirds::parse

#endif // LADYBIRDS_PARSE_EXPRMAP_H
