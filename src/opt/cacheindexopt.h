// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_OPT_CACHEINDEXOPT_H
#define LADYBIRDS_OPT_CACHEINDEXOPT_H

#include <iostream>
#include <memory>
#include <vector>
#include "../graph/graph.h"
#include "../spec/platform.h"

namespace Ladybirds{
    
namespace impl { class Buffer; struct Program; class TaskDivision; }
namespace spec { class Task; struct Platform; }
    
namespace opt{

//! Assign memory addresses to the buffers such that they do not have the same cache index
class CacheIndexOpt
{
private:
    spec::Platform::Cluster ClusterInfo_;
    spec::Platform::CacheConfig CacheConfig_;
    class BufferRelationGraph;
    std::unique_ptr<BufferRelationGraph> upBufferGraph_;
    struct BankInfo
    {
        int FreeSpace;
        int nBuffers;
        struct Slot { int Start; int End; };
        std::vector<Slot> Slots;
    };
    std::vector<BankInfo> Banks_;
    
public:
    //! Initializes the optimization
    CacheIndexOpt(const spec::Platform::Cluster & clusterinfo, const spec::Platform::CacheConfig & cacheinfo);
    CacheIndexOpt(const CacheIndexOpt &) = delete;   // by default
    CacheIndexOpt &operator=(const CacheIndexOpt &) = delete; // dito
    ~CacheIndexOpt(); //defined in cacheindexopt.cpp to be able to properly deallocate the graph
    
    //! Runs the optimization and provides the info that GenerateBufferGraphFile and PrintAssignmentInfo can give out
    bool Optimize(impl::TaskDivision &div);

    void GenerateBufferGraphFile();
    void PrintAssignmentInfo(std::ostream & strm);
    
private:
    struct ColorInfo {int count; int offset; int gap;};
    
    void CreateBufferGraph(impl::TaskDivision &div);
    bool FillBankInfo();
    ColorInfo GetColors();
    bool RunOptimization();
};

}} //namespace Ladybirds::opt

#endif // LADYBIRDS_OPT_CACHEINDEXOPT_H
