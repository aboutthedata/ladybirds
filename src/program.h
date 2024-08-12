// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef PROGRAM_H
#define PROGRAM_H

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "graph/graph.h"
#include "graph/itemmap.h"
#include "graph/itemset.h"
#include "graph/presdeque.h"
#include "dependency.h"
#include "kernel.h"
#include "loadstore.h"
#include "metakernel.h"
#include "task.h"
#include "buffer.h"


namespace Ladybirds { namespace impl {
struct Channel;
class TaskDivision;
 
struct Program : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(Program)
    
    struct Definition : public loadstore::LoadStorableCompound
    {
        std::string Identifier;
        std::string Value;
        
        Definition() = default;
        inline Definition(const std::string & id, const std::string & val) : Identifier(id), Value(val) {}
        inline Definition(std::string && id, std::string && val) : Identifier(id), Value(val) {}
        virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
    };
    using KernelList = std::unordered_map<std::string, spec::Kernel*>;
    using NativeKernelList = std::vector<std::unique_ptr<spec::Kernel>>;
    using MetaKernelList = std::vector<std::unique_ptr<spec::MetaKernel>>;
    using DepList = std::vector<spec::Dependency>;
    using DefList = std::vector<Definition>;
    using IntList = std::vector<int>;
    using BufferList = graph::PresDeque<Buffer>;
    using GroupList = std::vector<std::unique_ptr<TaskGroup>>;
    using ChannelList = std::vector<std::unique_ptr<Channel>>;
    using StringList = std::vector<std::string>;
    using DivisionList = std::vector<TaskDivision>;
    using TypeMap = std::unordered_map<std::string, spec::BaseType>;
    using ReachabilityMap = graph::ItemMap<graph::ItemSet>;
    using PassNameSet = std::set<std::string>;
    
    DefList Definitions;
    KernelList Kernels;
    NativeKernelList NativeKernels;
    NativeKernelList SpecialKernels;
    MetaKernelList MetaKernels;
    spec::Task MainTask;
    spec::TaskGraph TaskGraph;
    DepList Dependencies;
    DepList SpecialDependencies;
    ReachabilityMap TaskReachability;
    GroupList Groups;
    DivisionList Divisions;
    BufferList ExternalBuffers;
    ChannelList Channels;
    StringList CodeFiles, AuxFiles;
    TypeMap Types;
    PassNameSet PassesPerformed;
    
    Program(); //does not do much, but we need to define it in program.cpp to reduce dependencies
    ~Program(); //dito
    
    auto GetTasks() {return TaskGraph.Nodes(); }
    auto GetTasks() const {return TaskGraph.Nodes(); }
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
};

}} //namespace Ladybirds::impl

#endif // PROGRAM_H
