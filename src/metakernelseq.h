// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_PARSE_METAKERNELSEQ_H
#define LADYBIRDS_PARSE_METAKERNELSEQ_H

#include <deque>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "kernel.h"
#include "metakernel.h"
#include "range.h"

namespace Ladybirds { namespace spec { class Iface; }}
namespace clang { class ValueDecl; }

namespace Ladybirds
{
namespace parse
{

    
//! Direct representation of the Metakernels as specified in Ladybirds-C, i.e. as a sequence of calls to Kernels.
/** This is only an intermediate form, out of which a graph will be constructed. **/
class MetaKernelSeq
{
public:
    using Packet = spec::Packet;
    class KernelCall;
    struct Declaration
    {
        const spec::Packet * pVar = nullptr;
        int ParentIfaceIndex = -1;
    };
    
public:
    std::deque<spec::Packet> Variables;
    std::unordered_map<const clang::ValueDecl *, Declaration> DeclMap;
    std::unordered_set<const clang::ValueDecl *> GenVars;
    std::vector<KernelCall> Operations;
    spec::MetaKernel * pMetaKernel; ///< Points to the graph representation of the metakernel, which is to be created
    
public:
    MetaKernelSeq(spec::MetaKernel * pmetakernel);
    MetaKernelSeq ( const MetaKernelSeq &other ) = delete; //disabled by default
    MetaKernelSeq &operator= ( const MetaKernelSeq &other ) = delete; //dito
    
    //! Converts the metakernel from a sequential representation to a graph representation, and stores it in pMetaKernel
    bool TranslateToMetaKernel(std::string& errors);
};

class MetaKernelSeq::KernelCall
{
    friend class MetaKernelSeq;
public:
    using Kernel = spec::Kernel;
    using Range = gen::Range;
    
    class Argument
    {
        friend class MetaKernelSeq;
    public:
        using RangeVec = gen::Space;
        using RelDimVec = std::vector<int>;
        using ArrayDimVec = spec::Packet::ArrayDimVector;

    private:
        const Packet * Variable_;
        RangeVec Indices_;
        RelDimVec RelevantDims_;
        ArrayDimVec ResultingDim_;
        ArrayDimVec RequestedDim_;
        
        std::string ErrorDesc_;
        
        spec::Iface * Iface_ = nullptr;
        int BufferHint_ = -1;
        
    public:
        /** Default constructor. Builds all internal structures and checks the validity of the expression
         *  (whether it is a correct sub-array of the given variable).
         *  \p variable and \p indices must therefore be complete at this time already. 
         *  Consider using std::move for the latter. 
         *  For details on \p indices, see GetIndices. 
         * \p indices may however be shorter than the dimensions of \p variable; in this case, the full range is assumed.
         **/
        Argument(const Packet * variable, RangeVec indices);
        Argument(const Argument&) = delete; //by default
        Argument & operator=(const Argument&) = delete; //dito
        Argument(Argument&&) = default;
        Argument & operator=(Argument&&) = default;
        
        //! The variable which (or a subblock of which) is passed to the callee
        inline const Packet * GetVariable() const { return Variable_; }
        //! The index ranges of the sub-block of the variable passed to the callee.
        /** They are in the order of the declarations. **/
        inline const RangeVec & GetIndices() const { return Indices_; }
        //! The dimensions of the passed variable that are not collapsed away in the function call. 
        inline const RelDimVec & GetRelevantDims() const { return RelevantDims_; }
        //! The size of the resulting block (after the indexing operations)
        inline const ArrayDimVec & GetResultingDim() const { return ResultingDim_; }
        
        //! Returns true if the argument expression is valid.
        inline bool IsValid() const { return ErrorDesc_.empty(); }
        //! Returns an explanation string of why the expression is not valid, or an empty string if it is.
        inline const std::string & GetErrorDesc() const { return ErrorDesc_; }
        
        //! The index of the interface of the parent meta kernel that is used for this argument
        inline void SetBufferHint(int i) { BufferHint_ = i; }
    };
    
    using ArgVec = std::vector<Argument>;
    using ParamVec = std::vector<int>;

private:
    Kernel* Callee_;
    ArgVec Args_;
    ParamVec Params_, DerivedParams_;
    std::string ErrorDesc_;
    bool Valid_;
    
public:
    /** Default constructor. Builds all internal structures and checks the validity of the call
     *  (whether the arguments are as required by the callee).
     *  \p callee and \p args must therefore be complete at this time already. 
     *  Consider using std::move for the latter. 
     *  For details on \p args, see GetArguments. 
     **/
    KernelCall(Kernel * callee, ArgVec args, ParamVec params, ParamVec derivedparams);
    KernelCall(const KernelCall &) = delete; //by default
    KernelCall& operator=(const KernelCall&) = delete; //dito
    KernelCall(KernelCall&&) = default;
    KernelCall& operator=(KernelCall&&) = default;
    
    //! The kernel to be called by this operation
    inline Kernel* GetCallee() { return Callee_; }
    //! The kernel to be called by this operation
    inline const Kernel* GetCallee() const { return Callee_; }
    //! The arguments passed to the Kernel
    inline const ArgVec & GetArguments() const { return Args_; }
    //! The parameters passed to the kernel
    inline const ParamVec & GetParameters() const { return Params_; }
    //! The derived expressions calculated from the parameters passed to the kernel
    inline const ParamVec & GetDerivedParams() const { return DerivedParams_; }

    //! Returns true if the argument expression is valid.
    bool IsValid() const { return Valid_; }
    //! Returns an explanation string of why the expression is not valid, or an empty string if it is.
    const std::string & GetErrorDesc() const { return ErrorDesc_; }
};

void Dump(MetaKernelSeq::KernelCall & call);
    
}} //namespace Ladybirds::parse

#endif // LADYBIRDS_PARSE_METAKERNELSEQ_H
