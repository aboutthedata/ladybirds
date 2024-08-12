// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef TASKGROUP_H
#define TASKGROUP_H

#include <vector>
#include <unordered_map>

#include "spec/platform.h"
#include "dependency.h"
#include "task.h"
#include "loadstore.h"
#include "range.h"
#include "buffer.h"

using namespace std;
//template<typename t> using PropConst = std::experimental::propagate_const<t>;

namespace Ladybirds {

namespace impl {
struct Channel;


class Port : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(Port);
    
    using Iface = Ladybirds::spec::Iface;
private:
    Iface * Iface_ = nullptr;
    Channel * Chan_ = nullptr;
public:
    gen::Space Position;
    const Iface::BufferDimVec *BufferDims = nullptr;
    int BufferBaseTypeSize = 0;

public:
    inline Port() = default; //for later loading
    inline Port(Iface * piface) : Iface_(piface) {}
    Port(const Port &) = delete; //not allowed, would break const-protection of Iface
    Port & operator=(const Port&) = delete; //dito
    Port(Port &&) = default;
    Port & operator=(Port&&) = default;
    
    inline Iface* GetIface() { return Iface_; }
    inline const Iface* GetIface() const { return Iface_; }
    inline Channel* GetChannel() { return Chan_; }
    inline const Channel* GetChannel() const { return Chan_; }
    
    inline void Connect(Channel * pchan) {assert(!Chan_); Chan_ = pchan; }
    inline void Disconnect() { Chan_ = nullptr; }
    
    inline void Invalidate() { Iface_ = nullptr; }
    inline bool IsValid() { return Iface_; }
    
    virtual bool LoadStoreMembers(loadstore::LoadStore &ls) override;
};

struct Channel : loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(Channel);

    Port * From = nullptr;
    Port * To = nullptr;
    spec::Dependency *Dep = nullptr;
    
    //Channel() = default;
    Channel(Port * from, Port * to, spec::Dependency *dep);
    
    inline void Invalidate() { From->Invalidate(); To->Invalidate(); From = To = nullptr; }
    inline bool IsValid() const { return From && To; }
    
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
};

class TaskDivision;

//! A group of tasks, e.g. for forming a joint process in a KPN
class TaskGroup : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(TaskGroup);
    friend class TaskDivision;
    
    using Task = Ladybirds::spec::Task;
    using Dependency = Ladybirds::spec::Dependency;
    using Core = Ladybirds::spec::Platform::Core;
public:
    
    struct Operation : public loadstore::LoadStorableCompound
    {
        using PortList = std::vector<std::unique_ptr<Port>>;
        PortList Inputs, Outputs;
        Task* TheTask = nullptr; //TODO: Make this a propagate_const
        
        Operation() = default;
        Operation(Task * task) : TheTask(task) {};
        virtual bool LoadStoreMembers(loadstore::LoadStore &ls) override;
    };
    
    using OpList = std::vector<std::unique_ptr<Operation>>;
private:
    std::string Name_;
    int ID_;
    OpList Operations_;
    std::unordered_map<const Task*, int> TaskMap_;
    TaskDivision * Division_ = nullptr;
    const Core *CoreBinding_ = nullptr;
    
public:
    inline TaskGroup() = default;
    inline TaskGroup(int ID, std::string name) : Name_(std::move(name)), ID_(ID) {}
    
    //! Creates a group with only one single task as a member
    TaskGroup(Task* onlyMember);
    TaskGroup(const TaskGroup& other) = delete;             // disabled by default
    TaskGroup& operator=(const TaskGroup& other) = delete;  // dito
    TaskGroup(TaskGroup&& other) = default;
    TaskGroup& operator=(TaskGroup&& other) = default;
    
    //! Name of the group
    inline const std::string & GetName() const { return Name_; }
    inline void SetName(const std::string & name) { Name_ = name; }
    
    //! ID of the group
    inline int GetID() const { return ID_; }
    
    //! Returns true if \p task is a member of this group
    inline bool Contains(const Task * task) const { return TaskMap_.find(task) != TaskMap_.end(); }
    inline const OpList & GetOperations() { return Operations_; }
    
    void AddTask(Ladybirds::impl::TaskGroup::Task* task);
    inline int GetTaskCount() const { return TaskMap_.size(); }
    
    virtual bool LoadStoreMembers(loadstore::LoadStore& ls) override;
    
    //! Rearranges the order of the internal Task array (cf. GetTasks) according to \p newOrder.
    /** newOrder must contain each Task in this group exactly once. Other tasks may be contained in newOrder as well.**/
    void Reorder(const std::vector<std::unique_ptr<Task>> & newOrder);
    
    //! Invalidates unnecessary ports in the group as well as their "counterports"
    void SimplifyPorts();
    //! Removes invalid ports from the operations
    void PortCleanup();
    
    //! Adds an input port for the \p connection
    Port * AddInputPort(Dependency & connection);
    //! Adds an output port for the \p connection
    Port * AddOutputPort(Dependency & connection);
    
    //! The division this group belongs to (or nullptr)
    inline TaskDivision * GetDivision() { return Division_; }
    
    inline void Bind(const Core *pc) { CoreBinding_ = pc; } ///< Bind this group to a given PE in a platform
    inline const Core* GetBinding() { return CoreBinding_; } ///< Retrieve the PE to which this group has been bound
};

//! a group of task groups, e.g. to be mapped together to one cluster
class TaskDivision : public loadstore::Referenceable
{
    ADD_CLASS_SIGNATURE(TaskDivision);
public:
    graph::PresDeque<Buffer> Buffers;
    
private:
    std::vector<TaskGroup*> Groups_;
    mutable std::vector<spec::Task*> Tasks_;
    
public:
    TaskDivision() = default;
    TaskDivision(const TaskDivision &) = delete; //disabled by default
    TaskDivision &operator=(const TaskDivision &) = delete; //dito
    TaskDivision(TaskDivision &&);
    TaskDivision &operator=(TaskDivision &&);
    
    virtual bool LoadStoreMembers(loadstore::LoadStore &ls) override;
    
    inline const auto & GetGroups() { return Groups_; }
    inline const auto & GetTasks() { if(Tasks_.empty()) UpdateTasks(); return Tasks_; }
    
    inline void ReserveForMoreGroups(int howmany) { 
        Groups_.reserve(Groups_.size()+howmany); }
    inline void AddGroup(TaskGroup * pg)
    {
        assert(!pg->Division_); pg->Division_ = this; 
        Groups_.push_back(pg);
        InvalidateTasks();
    }
        
    inline void InvalidateTasks() { Tasks_.clear(); }
    
private:
    void UpdateTasks() const;
};

}} //namespace Ladybirds::impl

#endif // TASKGROUP_H
