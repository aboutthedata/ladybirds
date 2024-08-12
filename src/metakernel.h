// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_SPEC_METAKERNEL_H
#define LADYBIRDS_SPEC_METAKERNEL_H

#include <memory>
#include <vector>
#include "dependency.h"
#include "kernel.h"
#include "task.h"

namespace Ladybirds { namespace spec {

class MetaKernel : public Kernel
{
public:
    using TaskList = std::vector<std::unique_ptr<Task>>;
    using DepList = std::vector<Dependency>;
    
    TaskList Tasks;
    std::unique_ptr<Task> Inputs, Outputs;
    DepList Dependencies;

public:
    //! Creates an *uninitialized* metakernel. After setting up input/output packets, call InitInterface.
    MetaKernel() = default;
    MetaKernel(const MetaKernel& other); //want to copy and then modify meta-kernels
    MetaKernel& operator=(const MetaKernel& other) = delete; //by default
    MetaKernel(MetaKernel &&) = default;
    MetaKernel& operator=(MetaKernel&&) = default;
    
    virtual bool IsMetaKernel() const override { return true; }
    
    //! Initializes the Inputs and Outputs member variables. To be called when the Packets list from Kernel is complete.
    void InitInterface();
    
    //! Replaces the meta-kernel instance at \p it with its contents (i.e. its internal tasks and dependencies).
    /** The object pointed to by \p it must be task of this meta-kernel, and must be an instance of a meta-kernel.
        Returns an iterator to the task following \p it. All iterators at or after \p it are invalidated. **/ 
    TaskList::iterator Expand(TaskList::iterator itTask);
    //! Recursively expands all meta-kernel instance in this meta-kernel's task list (cf. Expand()).
    void Flatten();
    
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
};

}} // namespace Ladybirds::spec

#endif // LADYBIRDS_SPEC_METAKERNEL_H
