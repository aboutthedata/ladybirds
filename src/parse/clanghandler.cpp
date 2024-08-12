// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "clanghandler.h"
#include "clanghandlerfactory.h"
#include "clanghandler-kernelcallparser.h"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/Lexer.h>
#include "llvm/Support/Path.h"

#include <memory>
#include <string>
#include <vector>

#include "cinterface.h"
#include "kernel.h"
#include "metakernelseq.h"
#include "packet.h"
#include "program.h"
#include "range.h"
#include "tools.h"

using std::vector;
using std::string;
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


static std::string Stmt2Str(const clang::Stmt *d, clang::SourceManager &sm) {
    // (T, U) => "T,,"
    string text = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(d->getSourceRange()), sm, clang::LangOptions(), 0).str();
    if (text.at(text.size()-1) == ',')
        return clang::Lexer::getSourceText(clang::CharSourceRange::getCharRange(d->getSourceRange()), sm, clang::LangOptions(), 0).str();
    return text;
}

namespace Ladybirds{ namespace parse {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class ClangHandlerFactory

std::unique_ptr<clang::ASTConsumer> ClangHandlerFactory::newASTConsumer()
{
    return std::make_unique<ClangHandler>(Opts_, ProgRef_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class ClangHandler

class ClangHandler::MatchLambda : public clang::ast_matchers::MatchFinder::MatchCallback
{
    using ResultType = const clang::ast_matchers::MatchFinder::MatchResult &;
    using CBType = std::function<void(ResultType)>;
private:
    CBType Callback_;
public:
    MatchLambda(CBType callback) : Callback_(std::move(callback)) {};
    virtual void run(ResultType result) override { Callback_(result); }
};


/////////////////
// Main entry point
void ClangHandler::HandleTranslationUnit(ASTContext& context)
{
    if(context.getDiagnostics().hasErrorOccurred()) return;
    
    Context_ = &context;
    Rewriter_.setSourceMgr(context.getSourceManager(), context.getLangOpts());
    
    Parse();
    if(!Context_->getDiagnostics().hasErrorOccurred()) WriteTransformedCode();
    
    Context_ = nullptr; //we don't know if context is valid outside this function, so make sure we don't use it
    Rewriter_ = AnnotatingRewriter(); //reset
}

/////////////////
// High level funcionality


void ClangHandler::Parse()
{
    // Define the matchers for kernel declarations & invoke calls
    using namespace clang::ast_matchers;
    auto kernelFunctionMatcher = functionDecl(matchesName("_lb_(meta)?kernel_")).bind("kernel");
    auto invokeCallMatcher = callExpr(callee(functionDecl(matchesName("::invoke(seq)?$")))).bind("invoke");

    MatchLambda kernelFunctionCallback([this](const MatchFinder::MatchResult & result) 
        { ProcessKernelFunction(result.Nodes.getNodeAs<FunctionDecl>("kernel")); });
    MatchLambda invokeCallCallback([this](const MatchFinder::MatchResult & result) 
        { ProcessInvoke(result.Nodes.getNodeAs<clang::CallExpr>("invoke")); });

    // Add the matchers and their callback to the MatchFinder
    MatchFinder matchFinder;
    matchFinder.addMatcher(kernelFunctionMatcher, &kernelFunctionCallback);
    matchFinder.addMatcher(invokeCallMatcher, &invokeCallCallback);

    matchFinder.matchAST(*Context_);
}

void ClangHandler::WriteTransformedCode() const
{
    if(Options_.TranslationOutput.empty()) return; //no output required
    
    auto & sourceman = Rewriter_.getSourceMgr();
    auto fid = sourceman.getMainFileID();

    std::error_code err;
    llvm::raw_fd_ostream fileStream(Options_.TranslationOutput, err);
    if(err)
    {
        auto & diag = Context_->getDiagnostics();
        auto msgid = diag.getCustomDiagID(clang::DiagnosticsEngine::Fatal, "Unable to write to '%0': %1");
        diag.Report(msgid) << Options_.TranslationOutput << err.message();
        return;
    }
    
    if(Options_.Instrumentation) fileStream << "#define __LADYBIDRS_INSTRUMENTATION_AT_WORK__\n";
    Rewriter_.WriteWithAnnotations(fid, fileStream);
}


/////////////////
// Utility functions

//! \internal Internal visitor which tests each variable in a given expression on whether it is either
//! global and constant or a parameter, i.e. usable in a kernel definition to be folded to an integer constant
struct ClangHandler::DimExprVisitor : public clang::RecursiveASTVisitor<DimExprVisitor>
{
private:
    friend class clang::RecursiveASTVisitor<DimExprVisitor>;
    ClangHandler & ClangHandler_;
    ClangHandler::KernelExpressions & KernelExp_;
    bool Success_;
    
public:
    DimExprVisitor(ClangHandler & ch, ClangHandler::KernelExpressions & ke) : ClangHandler_(ch), KernelExp_(ke) {}
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
        
        if(pvar->getKind() == clang::Decl::ParmVar && KernelExp_.Params.find(pvar) != KernelExp_.Params.end())
            return true;
        if(pdre->isIntegerConstantExpr(*ClangHandler_.Context_)) return true;
        
        ClangHandler_.RaiseError(pdre->getLocation(), 
            "Only kernel parameters or constants are allowed here") << pdre << pdre->getSourceRange();
        Success_ = false;
        return true;
    }
};

clang::QualType ClangHandler::ExtractArrayType(clang::QualType type, vector<int> & dims, 
                                               KernelExpressions * pke, const clang::SourceRange & diagRange)
{
    if (auto *decayed = dyn_cast<const clang::DecayedType>(type.getTypePtr()))
        type = decayed->getOriginalType();
    
    DimExprVisitor dimcheck(*this, *pke);
    
    while (true)
    {
        switch(type->getTypeClass())
        {
            case clang::Type::ConstantArray:
            {
                auto *constantArray = cast<const clang::ConstantArrayType>(type.getTypePtr());
                dims.push_back(constantArray->getSize().getSExtValue());
                type = constantArray->getElementType();
                continue;
            }
            case clang::Type::VariableArray:
            {
                auto *varArray = cast<const clang::VariableArrayType>(type.getTypePtr());
                auto *psizeexpr = varArray->getSizeExpr();
                if(!pke)
                {
                    RaiseError(psizeexpr, "Expression cannot be evaluated to integer constant");
                    dims.push_back(INT_MAX);
                }
                else
                {
                    if(dimcheck.Test(psizeexpr))
                    {
                auto res = pke->Expressions.emplace(psizeexpr, pke->Expressions.size()+1);
                        dims.push_back(-res.first->second); //returned index, either existing one or new one
                    }
                    else dims.push_back(INT_MAX);
                }
                type = varArray->getElementType();
                continue;
            }
            case clang::Type::DependentSizedArray:
            {
                auto *dsArray = cast<const clang::DependentSizedArrayType>(type.getTypePtr());
                RaiseError(dsArray->getSizeExpr(), "Expression cannot be evaluated");
                dims.push_back(INT_MAX);
                type = dsArray->getElementType();
                continue;
            }
            case clang::Type::IncompleteArray:
            {
                auto *iArray = cast<const clang::IncompleteArrayType>(type.getTypePtr());
                RaiseError(diagRange.getBegin(), "Incomplete array declaration. "
                           "All dimensions of the array must be specified.") << diagRange;
                dims.push_back(INT_MAX);
                type = iArray->getElementType();
                continue;
            }
            default:;
        }
        break;
    }
    
    return type;
}

Packet ClangHandler::ExtractPacket(const clang::ValueDecl * decl, KernelExpressions *pke)
{
    //Packet name
    string name = decl->getNameAsString();
    
    //Array dimensions, also strip type
    vector<int> arraydims;
    clang::QualType qualType = ExtractArrayType(decl->getType(), arraydims, pke, decl->getSourceRange());
    
    //base type
    auto canonical = qualType.getTypePtr()->getCanonicalTypeInternal();
    string tname = canonical.getAsString();
    
    auto ittype = Program_.Types.find(tname);
    if(ittype == Program_.Types.end())
        ittype = Program_.Types.emplace(std::piecewise_construct, std::forward_as_tuple(tname), 
                                    std::forward_as_tuple(tname, (Context_->getTypeSize(canonical)+7)/8)).first;
    
      
    return Packet(name, Packet::invalid, &ittype->second, std::move(arraydims));
}

/////////////////
// Handling Kernels


void ClangHandler::ProcessKernelFunction(const FunctionDecl *functionDecl)
{
    assert(functionDecl);
    
    bool isforwarddecl = false;
    if(!functionDecl->isThisDeclarationADefinition())
    {
        if(functionDecl->hasBody()) return; //we have already parsed or will later parse the proper definition
        isforwarddecl = true; //No definition in this translation unit. We will need to create some kind of dummy.
    }
    
    if(isforwarddecl)
    {
        RaiseError(functionDecl->getLocation(), "Sorry, multi-file support not implemented yet. "
            "Please add a definition for this function.") << functionDecl->getSourceRange(); //TODO
    }
    
    Kernel * pkernel;
    if (functionDecl->getName().startswith("_lb_metakernel_"))
    {
        // Handle metakernel
        auto upmk = std::make_unique<spec::MetaKernel>();
        pkernel = upmk.get();

        pkernel->Name = functionDecl->getName().drop_front(15).str();
        GenerateKernelFromFunctionDecl(functionDecl, pkernel);
        upmk->InitInterface();
        
        MetaKernelSeq mks(upmk.get());

        int i = 0;
        for (const clang::ParmVarDecl *parm : functionDecl->parameters())
        {
            mks.DeclMap.emplace(parm, MetaKernelSeq::Declaration({&pkernel->Packets[i], i}));
            ++i;
        }
        ProcessMetakernelBody(*functionDecl, mks);
        UpdateSourceProperty(*functionDecl, *pkernel);
        
        string err;
        mks.TranslateToMetaKernel(err);
        if(!err.empty())
        {
            RaiseError(Context_->getSourceManager().getExpansionLoc(functionDecl->getLocation()), 
                       "Semantic problems in meta-kernel %0:\n%1") << pkernel->Name << err;
        }
        Program_.Kernels[upmk->Name] = pkernel;
        Program_.MetaKernels.push_back(std::move(upmk));
    }
    else if (functionDecl->getName().startswith("_lb_kernel_"))
    {
        // Handle kernel functions
        auto kernel = std::make_unique<Kernel>();
        kernel->Name = functionDecl->getName().drop_front(11).str();
        
        GenerateKernelFromFunctionDecl(functionDecl, kernel.get());
        UpdateSourceProperty(*functionDecl, *kernel);
        
        Program_.Kernels[kernel->Name] = pkernel = kernel.get();
        Program_.NativeKernels.push_back(std::move(kernel));
    }
    else
    {
        RaiseDiag(clang::DiagnosticsEngine::Fatal, functionDecl->getBeginLoc(),
                  "Internal error: Trying to process kernel not starting on '_lb_(meta)kernel'");
        exit(1);
    }
    
    TransformKernelDecl(functionDecl, pkernel);
}

// This method transforms the parameters of a kernel into a size array and a memory pointer:
//   unsigned char Par[10][20]     =>     int _lb_size_Par[2], void *_lb_name_Par
// It also adds a variable declaration to ensure consistency:
//   unsigned char (*Par)[ _lb_size_Par[1] ] = (unsigned char (*)[ _lb_size_Par[1] ])_lb_name_Par;
bool ClangHandler::TransformKernelDecl(const FunctionDecl *functionDecl, Kernel *pkernel)
{
    bool transformBody = false;
    clang::SourceLocation insertloc;
    if(functionDecl->doesThisDeclarationHaveABody())
    {
        auto body = llvm::cast<clang::CompoundStmt>(functionDecl->getBody());
        assert(body);
        if(!body->body_empty())
        {
            insertloc = body->body_front()->getBeginLoc();
            transformBody = true;
        }
    }
    
    auto * pke = &KernelInfo_.at(pkernel);
    auto arguments = functionDecl->parameters();
    int nparams = pkernel->Params.size();
    assert(nparams + pkernel->Packets.size() == arguments.size());
    
    for(int i = 0; i < nparams; ++i)
    {
        const auto *parm = arguments[i];
        // Change the kernel parameters
        Rewriter_.ReplaceText(Rewriter_.getSourceMgr().getExpansionRange(parm->getSourceRange()),
                              parm->getType().getAsString() + ' ' + parm->getNameAsString());
    }
    
    for (size_t i = nparams; i < arguments.size(); ++i)
    {
        const auto *parm = arguments[i];
        const auto name = parm->getNameAsString();
        vector<int> arraydims;
        auto type = ExtractArrayType(parm->getType(), arraydims, pke, parm->getSourceRange());
        int ndims = arraydims.size();
        const auto type_name = type.getAsString();
        
        // Change the kernel arguments
        if(Options_.Instrumentation)
        {
            string dimstring; dimstring.reserve(ndims*8);
            for(auto dim : arraydims) (dimstring += ", ") += std::to_string(dim);
            Rewriter_.ReplaceText(Rewriter_.getSourceMgr().getExpansionRange(parm->getSourceRange()),
                                  strprintf("Ladybirds::PacketInstrumentation<%s%s> %s",
                                            type_name.c_str(), dimstring.c_str(), name.c_str()));
        }
        else
        {
        Rewriter_.ReplaceText(Rewriter_.getSourceMgr().getExpansionRange(parm->getSourceRange()),
                              strprintf("const int _lb_size_%s[%d], %svoid *_lb_base_%s", name.c_str(), ndims, 
                                        type.isConstQualified() ? "const " : "", name.c_str()));
        }

        if(!transformBody) continue;
        
        if(!Options_.Instrumentation)
        {
        // Reassign the parameters to the proper variables
        string arraydecl;
        for (int dim = ndims; dim-- > 1; )
            arraydecl += "[ _lb_size_" + name + "[" + std::to_string(dim) + "] ]";
        
        string varDecl = strprintf("%s (*%s)%s = (%s (*)%s) _lb_base_%s;\n",
                                   type_name.c_str(), name.c_str(), arraydecl.c_str(), 
                                   type_name.c_str(), arraydecl.c_str(), name.c_str());
    
        Rewriter_.InsertText(insertloc, varDecl, true, true);
    }
    }
    if(!transformBody) return true;
    
    if(Options_.Instrumentation)
    {
        if(pkernel->IsMetaKernel())
            Rewriter_.InsertText(insertloc, "Ladybirds::TaskInstrumentation _lb_tinstr;\n\n", true, true);
    }
    else
    {
        if(!arguments.empty())
            Rewriter_.InsertText(insertloc, "\n", true, true);
    }

    return true;
}

// This method generates a Ladybird-Kernel from its declaration
bool ClangHandler::GenerateKernelFromFunctionDecl(const FunctionDecl *functionDecl, Kernel *pkernel)
{
    bool ret = true;
    
    pkernel->FunctionName = pkernel->Name;//functionDecl->getNameAsString();
    auto lbfile = llvm::sys::path::filename(Rewriter_.getSourceMgr().getFilename(
        Rewriter_.getSourceMgr().getExpansionLoc(functionDecl->getLocation())));
    pkernel->CodeFile = (lbfile + ".c").str();
    std::map<string, Packet::AccessType> accesstypes = 
        {{"in", Packet::in}, {"out", Packet::out}, {"inout", Packet::inout}, {"param", Packet::param}};
    
    auto res = KernelInfo_.emplace(pkernel, KernelExpressions(functionDecl, Context_));
    assert(res.second);
    auto *pke = &res.first->second;
    bool foundpacket = false;
    
    for (const clang::ParmVarDecl *parm : functionDecl->parameters())
    {
        Packet packet = ExtractPacket(parm, pke);
        packet.SetKernel(pkernel);

        if(parm->hasAttrs())
        {
            for (const auto *attr : parm->getAttrs())
            {
                if (auto *annotateAttr = dyn_cast<const clang::AnnotateAttr>(attr))
                {
                    auto anno = annotateAttr->getAnnotation();
                    
                    auto it = accesstypes.find(anno.str());
                    if(it != accesstypes.end())
                    {
                        if(packet.GetAccessType() != Packet::invalid)
                            RaiseError(attr->getLocation(), "Redefinition of access type") << attr->getRange();
                        {
                            ret = false;
                        }
                        packet.SetAccessType(it->second);
                    }
                    else if(anno.startswith("buddy="))
                    {
                        auto buddyname = anno.drop_front(6);
                        auto buddy = pkernel->PacketByName(buddyname.str());
                        if(!buddy)
                        {
                            RaiseError(attr->getLocation(), "Undeclared buddy packet '%0'")
                                << attr->getRange() << buddyname;
                            ret = false;
                        }
                        else if(!packet.AddBuddy(buddy))
                        {
                            RaiseError(attr->getLocation(), "Buddy '%0' already defined") 
                                << attr->getRange() << buddyname;
                            ret = false;
                        }
                    }
                }
            }
        }
        if(packet.GetAccessType() == Packet::invalid)
        {
            RaiseError(parm->getSourceRange().getBegin(), "Missing access type specifier (put 'in', 'out' or 'inout')") 
                << parm->getSourceRange();
            packet.SetAccessType(Packet::inout);
        }
        
        if(packet.GetAccessType() != Packet::param)
        {
            pkernel->Packets.push_back(std::move(packet));
            foundpacket = true;
        }
        else
        {
            if(foundpacket)
            {
                RaiseError(parm->getSourceRange().getBegin(), "Parameter after packet argument."
                           "Please declare first all parameters, then all packets.") << parm->getSourceRange();
                ret = false;
            }
            if(pkernel->IsMetaKernel())
            {
                RaiseError(parm->getSourceRange().getBegin(), "Parameters are currently not supported for meta-kernels")
                    << parm->getSourceRange();
                ret = false;
            }
            if(!packet.GetArrayDims().empty())
            {
                RaiseError(parm->getSourceRange().getBegin(), "Parameters must not be arrays")
                    << parm->getSourceRange();
                packet = Packet(packet.GetName(), packet.GetAccessType(),
                                &packet.GetBaseType(), Packet::ArrayDimVector());
                ret = false;
            }
            if(!parm->getType()->isIntegerType())
            {
                RaiseError(parm->getSourceRange().getBegin(), "Parameters must be of integral types")
                    << parm->getSourceRange();
                ret = false;
                auto ittype = Program_.Types.emplace(std::piecewise_construct, std::forward_as_tuple("int"),
                                                     std::forward_as_tuple("int", 4)).first;
                packet = Packet(packet.GetName(), packet.GetAccessType(),
                                &ittype->second, Packet::ArrayDimVector());
            }
            pkernel->Params.push_back(std::move(packet));
            pke->Params.insert(parm);
        }
    }
    
    pkernel->DerivedParams.resize(pke->Expressions.size());
    for(auto pair : pke->Expressions)
    {
        pkernel->DerivedParams[pair.second-1] = Stmt2Str(pair.first, Context_->getSourceManager());
    }
    
    return ret;
}

/// Updates the SourceCode property of a kernel object
void ClangHandler::UpdateSourceProperty(const FunctionDecl &kernelDecl, Kernel &kernel)
{
    auto range = kernelDecl.getSourceRange();
    auto exprange = Context_->getSourceManager().getExpansionRange(range);
    kernel.SourceCode = Rewriter_.getRewrittenText(exprange);
}


void ClangHandler::ProcessInvoke(const clang::CallExpr * callExpr)
{
    assert(callExpr);
    
    if (callExpr->getNumArgs() != 1)
    {
        RaiseError(callExpr, "Unexpected number of arguments (%0) passed to 'invoke'") << callExpr->getNumArgs();
        return;
    }
    
    auto *innerCallExpr = dyn_cast<clang::CallExpr>(callExpr->getArg(0));
    if (!innerCallExpr)
    {
        RaiseError(callExpr->getArg(0), "Invalid argument to 'invoke': Must be a kernel call");
        return;
    }
    
    KernelCallParser parse(*this, MainMetakernel_);
    parse.ProcessKernelCall(innerCallExpr, true, false);
    
    if(callExpr->getDirectCallee()->getName() == "invokeseq")
        return;//success
    
    
    if(Program_.MainTask.GetKernel())
    {
        RaiseDiag(clang::DiagnosticsEngine::Warning, callExpr->getBeginLoc(),
                  "Currently only one invoke is suported. Overriding old main invoke...");
    }

    auto &kernelcall = MainMetakernel_.Operations.back();
    Program_.MainTask = spec::Task(kernelcall.GetCallee(), "main",
                                   kernelcall.GetParameters(), kernelcall.GetDerivedParams());
}


}} //namespace Ladybirds::parse
