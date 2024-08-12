// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_H_
#define LADYBIRDS_H_

#if defined(__cplusplus) && defined(__LADYBIDRS_INSTRUMENTATION_AT_WORK__)

#include <array>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace Ladybirds{
struct RWCount {int R=0, W=0; };



class ProgramInstrumentation
{
    friend class TaskInstrumentation;
    template<typename bt, int i, int... dims> friend class PacketInstrumentation;
private:
    std::ofstream Output;
    
public:
    ProgramInstrumentation()
    {
        const char* outfile = getenv("AccessOutput");
        if(!outfile) return;
        
        Output.open(outfile);
        if(!Output.is_open())
        {
            perror(outfile);
            exit(1);
        }
        
        Output << "accesses = {\n";
    }
    ~ProgramInstrumentation()
    {
        Output << "}\n";
        Output.close();
    }
};
static ProgramInstrumentation InstrumentationObject;


template<typename Basetype, int Dimension1 = 0, int... MoreDimensions>
class PacketInstrumentationBase
{
    template<typename bt, int i, int... dims> friend class PacketInstrumentationBase;
protected:
    using VoidT = typename std::conditional<std::is_const<Basetype>::value, const void*, void*>::type;
    
protected:
    Basetype *Base_;
    const int *Blocksizes_;
    const char * Packetname_;
    RWCount &Accesses;
    
protected:
    PacketInstrumentationBase(const char *varname, VoidT base, const int blocksizes[], RWCount &rw)
        : Packetname_(varname), Base_((Basetype*) base), Blocksizes_(blocksizes), Accesses(rw) {}
    template<int... Dimensions>
    PacketInstrumentationBase(const char *varname, PacketInstrumentationBase<Basetype, Dimensions...> base, 
                          const int blocksizes[], RWCount &rw)
        : Packetname_(varname), Base_((Basetype*) base.Base_), Blocksizes_(blocksizes), Accesses(rw) {}

        
public:
    PacketInstrumentationBase<Basetype, MoreDimensions...> operator[] (unsigned long index)
    {
        if(Dimension1 > 0 && index >= Dimension1) std::cerr << "WARNING: Variable access out of bounds for packet " << Packetname_ << std::endl;
        unsigned long addrmul = Blocksizes_[0];
        for(auto i = sizeof...(MoreDimensions); i > 0; --i) addrmul *= Blocksizes_[i];
        return PacketInstrumentationBase<Basetype, MoreDimensions...>(Packetname_, Base_+(index*addrmul), Blocksizes_, Accesses);
    }
    
    const int *size() const { return Blocksizes_; }
    
    PacketInstrumentationBase &operator=(const Basetype &newval)
    {
        Accesses.W++;
        *Base_ = newval;
        return *this;
    }
    operator const Basetype&()
    {
        Accesses.R++;
        return *Base_;
    }
    
    constexpr static int Mul(int i) { return i; }
    template<typename... Factors>
        constexpr static int Mul(int factor1, Factors... factors) { return factor1 * Mul(factors...); };
    
    Basetype *rawread() { Accesses.R += Mul(Dimension1, MoreDimensions...); return Base_; }
    Basetype *rawwrite() { Accesses.W += Mul(Dimension1, MoreDimensions...); return Base_; }
};

template<typename Basetype, int Dimension1 = 0, int... MoreDimensions>
class PacketInstrumentation : public PacketInstrumentationBase<Basetype, Dimension1, MoreDimensions...>
{
    template<typename bt, int i, int... dims> friend class PacketInstrumentation;
    using DimArray = std::array<const int, sizeof...(MoreDimensions)+1>;
    using Base = PacketInstrumentationBase<Basetype, Dimension1, MoreDimensions...>;
    
private:
    RWCount RWSlot;
    DimArray DimSlot;
    
public:
    PacketInstrumentation(const char *varname, typename Base::VoidT base, DimArray blocksizes)
        : DimSlot(std::move(blocksizes)), Base(varname, base, DimSlot.data(), RWSlot) {}
    template<int... Dimensions>
    PacketInstrumentation(const char *varname, PacketInstrumentationBase<Basetype, Dimensions...> base, DimArray blocksizes)
        : DimSlot(std::move(blocksizes)), Base(varname, base, DimSlot.data(), RWSlot) {}
    
    ~PacketInstrumentation()
    {
        InstrumentationObject.Output << this->Packetname_ << " = {" << this->Accesses.R << ',' << this->Accesses.W << "}, ";
    }
};

class TaskInstrumentation
{
protected:
    static std::string Callstack_;
    std::unordered_map<const char*, int> CallCounts_;
    
public:
    struct Closebracket { inline ~Closebracket() { InstrumentationObject.Output << " },\n"; } };
    Closebracket Call(const char *taskname)
    {
        InstrumentationObject.Output << "[\"" << Callstack_ << taskname << '[' << CallCounts_[taskname]++ << "]\"] = { ";
        return Closebracket();
    }
    
    struct ReduceStack
    {
        std::string::size_type oldlen;
        ~ReduceStack() { Callstack_.resize(oldlen); }
    };
    ReduceStack CallMeta(const char *taskname)
    {
        auto oldstacklen = Callstack_.length();
        Callstack_.resize(oldstacklen + 128);
        auto addlen = snprintf(const_cast<char*>(Callstack_.data())+oldstacklen, 128, "%s[%d].", taskname, CallCounts_[taskname]++);
        Callstack_.resize(oldstacklen+addlen);
        return {oldstacklen};
    }
};



std::string TaskInstrumentation::Callstack_;

} //namespace Ladybirds

#define _LB_RAWREAD(x) ((x).rawread())
#define _LB_RAWWRITE(x) ((x).rawwrite())
#else
#define _LB_RAWREAD(x) (x)
#define _LB_RAWWRITE(x) (x)
#endif


#ifdef __LADYBIDRS_PARSER_AT_WORK__

#ifdef __cplusplus
#define _LB_PROTO ...
#else
#define _LB_PROTO
#endif

void invoke(int);
void invokeseq(int);
#define kernel(x) int x(_LB_PROTO); void _lb_kernel_##x
#define metakernel(x) int x(_LB_PROTO); void _lb_metakernel_##x
#define in __attribute__((annotate("in"))) const
#define out __attribute__((annotate("out")))
#define inout __attribute__((annotate("inout")))
#define buddy(buddypacket) __attribute__((annotate("buddy="#buddypacket)))
#define param __attribute__((annotate("param"))) const
#define genvar __attribute__((annotate("genvar")))

#else //def __LADYBIDRS_PARSER_AT_WORK__

#define kernel(x) void x
#define metakernel(x) void x
#define in const
#define out
#define inout
#define buddy(buddypacket)
#define param const
#define genvar
#define invoke(x) (x)
#define invokeseq(x) (x)

#endif //def __LADYBIDRS_PARSER_AT_WORK__

#if defined(__GNUC__) && !defined(__clang__)
#define _LB_HIDDEN(x) 0
#else
#define _LB_HIDDEN(x) x
#endif

void fromfile(void *data, int size, const char *filename);
#define fromfile(data, size, filename) fromfile(_LB_RAWWRITE(data), size, filename)

#endif //LADYBIRDS_H_
