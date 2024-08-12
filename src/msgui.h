// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef MSGUI_H
#define MSGUI_H

#include <cstdio>
#include <iostream>
#include <memory>


class MsgUI
{
private:
    class OutputImpl;
    std::unique_ptr<OutputImpl> Output_, Verbose_;
    int NumErrors_ = 0;

public:
    MsgUI(FILE *outfile, FILE *verboseFile = nullptr);
    void open(FILE *outfile, FILE *verboseFile = nullptr);
    
    bool IsVerbose() const;
    
    std::ostream & Fatal(const char * msg = nullptr, ...);
    std::ostream & Error(const char * msg = nullptr, ...);
    std::ostream & Warning(const char * msg = nullptr, ...);
    std::ostream & Info(const char * msg = nullptr, ...);
    std::ostream & Verbose(const char * msg = nullptr, ...);
    
private:
    void PrintMsg(const char * classification, const char * msg, va_list args);
};

extern MsgUI gMsgUI;

#endif // MSGUI_H
