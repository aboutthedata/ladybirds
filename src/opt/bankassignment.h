// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_OPT_BANKASSIGNMENT_H
#define LADYBIRDS_OPT_BANKASSIGNMENT_H

#include <iostream>
#include <memory>
#include <vector>
#include "../graph/graph.h"

namespace Ladybirds{
    
namespace impl { class Buffer; struct Program; class TaskDivision; }
namespace spec { class Task; }
    
namespace opt{

class BankAssignment
{
private:
    impl::Program & Prog_;
    
    static constexpr int InitialBankCapacity = 116*1024;
    int BankCount_;

    struct TaskOverlap { spec::Task *Task1, *Task2; unsigned long Overlap; };
    std::vector<TaskOverlap> TaskOverlaps_;

    std::unique_ptr<class BufferRelationGraph> upBufferGraph_;
    
    int DumpCounter_ = 0;
    
public:
    BankAssignment(impl::Program & prog, int nbanks);
    BankAssignment(const BankAssignment &) = delete;   // by default
    BankAssignment &operator=(const BankAssignment &) = delete; // dito
    ~BankAssignment();

    bool LoadOverlaps(const char * filename);
    void CreateBufferGraph(impl::TaskDivision &div);

    bool AssignBanks(int correction = 0);

    void GenerateBufferGraphFile();
    void PrintAssignmentInfo(std::ostream & strm);
};

}} //namespace Ladybirds::opt

#endif // LADYBIRDS_OPT_BANKASSIGNMENT_H
