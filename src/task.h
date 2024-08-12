// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef TASK_H
#define TASK_H

#include <ostream>
#include <memory>
#include <string>
#include <vector>

#include "graph/graph.h"
#include "loadstore.h"
#include "range.h"

namespace Ladybirds {
    
namespace impl {
    class TaskGroup;
    class Buffer;
}
    
namespace spec {

class Kernel;
class Packet;
class Task;

class Iface : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(Iface);
    friend class Task;
    
public:
    static constexpr int OffsetNA = INT_MIN;
    using BufferDimVec = std::vector<int>;
    using ArrayDimVec = std::vector<int>;
    using BuddyList = std::vector<const Iface*>;

private:
    Task * Task_ = nullptr;
    Packet * Packet_ = nullptr;
    ArrayDimVec Dimensions_;
    impl::Buffer * Buffer_ = nullptr;
    
    std::shared_ptr<const BufferDimVec> BufferDims_;
    BufferDimVec BufferDimsAdj_;
    int BufferOffset_ = OffsetNA;

public:
    gen::Space PosHint; ///< A hint for the position of the interface within the buffer
    int BufferHint; ///< A hint for the buffer to be used
    
    int Reads = 0, Writes = 0; ///< Number of read and write accesses to this interface
    
public:
    inline Iface() { assert(false); } //!< DO NOT USE!
    inline Iface(Task * task, Packet * packet, ArrayDimVec dimensions)
            : Task_(task), Packet_(packet), Dimensions_(std::move(dimensions)) {}
    
    Iface(const Iface &) = delete;            //disabled by default
    Iface & operator=(const Iface&) = delete; //dito
    Iface(Iface &&) = default; //moving is allowed
    Iface& operator=(Iface &&) = default; //moving is allowed
    
private:
     //! To be used by Task only. For replicating a task. Creates a copy of the old interface, but with a new "owner" (task).
    Iface(Task * ptask, const Iface & other);

public:
    //! Task to which this interface belongs
    inline Task * GetTask() {return Task_;}
    //! \copydoc GetTask
    inline const Task * GetTask() const {return Task_;}
    //! The index this interface has in the interface list of the owning task
    //inline int GetIfaceIndex() const {return IfaceIndex_;}
    
    //! The buffer which will carry packets produced/consumed by this interface
    inline impl::Buffer* GetBuffer() { return Buffer_; }
    //! \copydoc GetBuffer
    inline const impl::Buffer* GetBuffer() const { return Buffer_; }
    //! The dimensions of the buffer
    inline const BufferDimVec & GetBufferDims() const { return *BufferDims_; }
    //! The dimensions of the buffer, collapsed for this interface.
    inline const BufferDimVec & GetBufferDimsAdj() const { return BufferDimsAdj_; }
    //! The offset denoting the position at which the packets produced/consumed by this interface are placed in the buffer
    inline int GetBufferOffset() const { return BufferOffset_; }
    //! Sets buffer, multiplication vector and offset (cf. GetBuffer, GetBufferDimsAdj and GetBufferOffset)
    inline void SetBuffer(impl::Buffer * pt, std::shared_ptr<const BufferDimVec> dims,
                           BufferDimVec dimsadj, int offset)
    {
        Buffer_ = pt; BufferDims_ = std::move(dims);
        BufferDimsAdj_ = std::move(dimsadj); BufferOffset_ = offset;
    }
    //! Replaces the buffer with a new one (e.g. when merging buffers)
    inline void RelocateBuffer(impl::Buffer * pt) { Buffer_ = pt; }

    
    //! Name of the interface. Currently the same as the name of the packet.
    const std::string & GetName() const;
    //! Full name of the interface (i.e. task name + '.' + interface name)
    std::string GetFullName() const;
    inline const Packet* GetPacket() const {return Packet_; }; ///< The packet which is expected/delivered on this iface
    //! The dimensions of the packet expected/delivered on this interface
    /**(for flexible kernels, this may vary between different instances)**/
    inline const ArrayDimVec & GetDimensions() const { return Dimensions_; };
    int GetMemSize() const;
    BuddyList GetBuddies() const;
    
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
};

std::ostream & operator <<(std::ostream & strm, Iface & iface);



class Task;
using TaskDependency = graph::Edge<Task>;
using TaskGraph = graph::Graph<Task>;

class Task : public graph::Node<TaskGraph, TaskDependency>, public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(Task);
    using basenode = graph::Node<TaskGraph, TaskDependency>;
    
private:
    Kernel * Kernel_ = nullptr;

    std::vector<int> Params_;
    std::vector<int> DerivedParams_;
    
public:
    std::string Name;
    double Cost = 0;
    
    std::vector<Iface> Ifaces;
    
    impl::TaskGroup * Group = nullptr; //TODO: make this a propagate_const
    
    
    Task() = default; //constructs an invalid object
    inline Task(Kernel * kernel, const std::string & name, std::vector<int> parameters, std::vector<int> derivedParams)
        : Kernel_(kernel), Params_(std::move(parameters)), DerivedParams_(std::move(derivedParams)), Name(name)
        { FillIfaces(); }
        
    Task(const Task& other);                     // have to copy tasks when we want to expand meta-kernels
    Task& operator=(const Task& other) = delete; // by default
    Task(Task&& other);
    Task& operator=(Task&& other);
    
    //! Returns the kernel which this task is an instance of
    inline Kernel * GetKernel() { return Kernel_; }
    inline const Kernel * GetKernel() const { return Kernel_; }
    
    //! Returns the parameters used for instantiating the kernel into this task
    inline const auto & GetParameters() const { return Params_; }
    //! Returns the derived parameters for this task
    /**(they are derived from the other parameters and required for variablearray dimensions)**/
    inline const auto & GetDerivedParameters() const { return DerivedParams_; }
    std::string GetFullName() const;
    
    Iface * GetIfaceByName(const std::string & name);
    inline const Iface * GetIfaceByName(const std::string & name) const
        {return const_cast<Task*>(this)->GetIfaceByName(name); }
    
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
    
private:
    void FillIfaces();
};



}}//namespace Ladybirds::spec


#endif // TASK_H
