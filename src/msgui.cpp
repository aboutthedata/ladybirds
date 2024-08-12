// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "msgui.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

MsgUI gMsgUI(stderr);

class MsgUI::OutputImpl : private std::basic_streambuf<char, std::char_traits<char> >
{
public:
    OutputImpl(FILE * f);
    
    inline FILE* FileHandle() { return File_; }
    inline std::ostream & Stream(){ return Stream_; }
    
private:
    static constexpr int eof = std::char_traits<char>::eof();
    virtual int overflow (int __c = eof);
    virtual int sync();
    
private:
    FILE* File_;
    std::ostream Stream_;
    char buf[256];
};


MsgUI::OutputImpl::OutputImpl(FILE * f) : File_(f), Stream_(this)
{
    setp(buf, buf+sizeof(buf)-1);
}

int MsgUI::OutputImpl::overflow(int __c)
{
    if(!File_)
    {
        setp(pbase(), epptr());
        return std::char_traits<char>::not_eof(__c);
    }
    
    auto base = pbase(), ptr = pptr();
    if(__c != eof) *(ptr++) = (char) __c;
    
    size_t len = ptr - base;
    if (fwrite(base, 1, len, File_) != len) return eof;
    
    setp(base, epptr());
    return std::char_traits<char>::not_eof(__c);
}

int MsgUI::OutputImpl::sync()
{
    if(!File_) return 0;
    overflow(eof);
    if (fflush(File_)) return -1;
    return 0;
}



MsgUI::MsgUI(FILE *outfile, FILE *verboseFile) 
{
    open(outfile, verboseFile);
}

void MsgUI::open(FILE* outfile, FILE* verboseFile)
{
    Output_ = std::make_unique<OutputImpl>(outfile);
    Verbose_ = std::make_unique<OutputImpl>(verboseFile);
}

bool MsgUI::IsVerbose() const
{
    return (Verbose_->FileHandle() != nullptr);
}


std::ostream & MsgUI::Fatal(const char *msg, ...)
{
    va_list va; va_start(va, msg);
    PrintMsg("Fatal error", msg, va);
    va_end(va);
    return Output_->Stream();
}

std::ostream & MsgUI::Error(const char *msg, ...)
{
    va_list va; va_start(va, msg);
    PrintMsg("Error", msg, va);
    va_end(va);
    
    constexpr int limit = 1000;
    if(++NumErrors_ > limit)
    {
        fprintf(Output_->FileHandle(), "More than %d errors. Exiting.\n", limit);
        exit(1);
    }
    return Output_->Stream();
}

std::ostream & MsgUI::Warning(const char *msg, ...)
{
    va_list va; va_start(va, msg);
    PrintMsg("Warning", msg, va);
    va_end(va);
    return Output_->Stream();
}

std::ostream & MsgUI::Info(const char *msg, ...)
{
    va_list va; va_start(va, msg);
    PrintMsg("Info", msg, va);
    va_end(va);
    return Output_->Stream();
}

std::ostream & MsgUI::Verbose(const char *msg, ...)
{
    auto f = Verbose_->FileHandle();
    if(!f || !msg) return Verbose_->Stream();
    va_list va; va_start(va, msg);
    vfprintf(f, msg, va);
    va_end(va);
    fputc('\n', f);
    return Output_->Stream();
}

void MsgUI::PrintMsg(const char *classification, const char *msg, va_list args)
{
    if(!msg) return;
    
    auto f = Output_->FileHandle();
    fputs(classification, f);
    fputs(": ", f);
    vfprintf(f, msg, args);
    fputc('\n', f);
}
