// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "taskgroup.h"

#include <numeric>
#include <unordered_set>
#include <vector>

#include "packet.h"
#include "task.h"
#include "buffer.h"

using namespace Ladybirds::spec;

namespace Ladybirds { namespace impl {

bool Port::LoadStoreMembers(loadstore::LoadStore &ls)
{
    assert(ls.IsStoring() && "loading not supported");
    auto dims = Position.GetDimensions();
    Iface::BufferDimVec bufferdims;
    if(BufferDims) bufferdims = *BufferDims;
    int offset = 0;

    if(!bufferdims.empty())
    {
        bufferdims.back() *= BufferBaseTypeSize;
        dims.back() *= BufferBaseTypeSize;
    
        assert(dims.size() == bufferdims.size());
        auto itpos = Position.begin();
        for(auto ittd = std::next(bufferdims.begin()), ittdend = bufferdims.end(); ittd != ittdend; ++ittd, ++itpos)
        {
            offset = (offset + itpos->begin()) * *ittd;
        }
        offset += itpos->begin()*BufferBaseTypeSize;
    }
    
    return ls.IORef("iface", Iface_)
         & ls.IO("dims", dims, false)
         & ls.IO("bufferdims", bufferdims, false)
         & ls.IO("offset", offset, false);
}

Channel::Channel(Port * from, Port * to, spec::Dependency *pdep)
    : From(from), To(to), Dep(pdep)
{
    assert(from->IsValid() && to->IsValid());
    from->Connect(this);
    to->Connect(this);
}



bool Channel::LoadStoreMembers(loadstore::LoadStore& ls)
{
    bool hasdata = (Dep != nullptr);
    return ls.IORef("from", From) & ls.IORef("to", To) & ls.IO("hasdata", hasdata, false, true);
}


bool TaskGroup::Operation::LoadStoreMembers(loadstore::LoadStore &ls)
{
    return ls.IORef("task", TheTask) & ls.IO_Register("inputs", Inputs) & ls.IO_Register("outputs", Outputs);
}


TaskGroup::TaskGroup(Task *onlyMember)
{
    TaskMap_[onlyMember] = 0;
    Operations_.emplace_back(std::make_unique<Operation>());
    Operations_.back()->TheTask = onlyMember;
    onlyMember->Group = this;
}



bool TaskGroup::LoadStoreMembers(loadstore::LoadStore& ls)
{
    std::vector<Task*> tasks; tasks.reserve(Operations_.size());
    for(auto & op : Operations_) tasks.push_back(op->TheTask);
    
    bool ret = ls.IO("name", Name_, false)
             & ls.IORef("members", tasks) 
             & ls.IO("operations", Operations_, false);
    
    if(ls.IsStoring()) return ret;
    
    assert(TaskMap_.empty() && Operations_.empty());
    TaskMap_.reserve(tasks.size()); Operations_.reserve(tasks.size());
    
    for(auto ptask : tasks)
    {
        if(ptask->Group && ptask->Group != this)
        {
            ls.Error("Task '%s' already belongs to another group.", ptask->GetFullName().c_str());
            ret = false;
        }
        else
        {
            TaskMap_[ptask] = Operations_.size();
            
            Operations_.emplace_back(std::make_unique<Operation>());
            Operations_.back()->TheTask = ptask;
            
            ptask->Group = this;
        }
    }
        
    return ret;
}

void TaskGroup::AddTask(TaskGroup::Task* task)
{
    auto res = TaskMap_.emplace(task, TaskMap_.size());
    assert(res.second == true);//Task was not already contained
    (void) res; //silence unused variable warning if asserts are disabled
    Operations_.emplace_back(std::make_unique<Operation>(task));
    if(Division_) Division_->InvalidateTasks();
}


void TaskGroup::Reorder(const std::vector<std::unique_ptr<Task>>& newOrder)
{
    OpList ops;
    ops.reserve(Operations_.size());
    
    for(auto & ptask : newOrder)
    {
        auto it = TaskMap_.find(ptask.get());
        if(it != TaskMap_.end())
        {
            ops.push_back(std::move(Operations_[it->second]));
            it->second = ops.size()-1;
        }
    }
    
    assert(ops.size() == TaskMap_.size());
    Operations_ = std::move(ops);
}

Port *TaskGroup::AddInputPort(Dependency &connection)
{
    Iface * piface = connection.To.TheIface;
    auto it = TaskMap_.find(piface->GetTask());
    assert(it != TaskMap_.end());
    Operation & op = *Operations_[it->second];
    op.Inputs.emplace_back(std::make_unique<Port>(piface));
    return op.Inputs.back().get();
}

Port *TaskGroup::AddOutputPort(Dependency &connection)
{
    Iface * piface = connection.From.TheIface;
    auto it = TaskMap_.find(piface->GetTask());
    assert(it != TaskMap_.end());
    Operation & op = *Operations_[it->second];
    op.Outputs.emplace_back(std::make_unique<Port>(piface));
    return op.Outputs.back().get();
}




/** For each input and output (i.e. same iface), look for multiple channels that come from/go to the same task group.
 *  Of those, we can eliminate the last/the first one, respectively. We do this by invalidating the ports for now;
 *  clean-up will happen later in one single go. **/
void TaskGroup::SimplifyPorts()
{
    //helper for sorting. Compares two ports, first by iface, then by connected task group,
    //then by position in this group
    struct compareports
    {
        Task* (*gettask)(const unique_ptr<Port>&);
        bool (*poscmp) (int,int);
        
        bool operator()(const unique_ptr<Port> & upin1, const unique_ptr<Port> & upin2)
        {
            Iface *iface1 = upin1->GetIface(), *iface2 = upin2->GetIface();
            if(iface1 != iface2) return (iface1 < iface2);
            if(!iface1) return true;
            
            Task *task1 = gettask(upin1), *task2 = gettask(upin2);
            TaskGroup *grp1 = task1->Group, *grp2 = task2->Group;
            if(grp1 != grp2) return grp1 < grp2;
            
            return poscmp(grp1->TaskMap_[task1], grp1->TaskMap_[task2]);
        }
    };
    
    
    //In a sorted list of ports, invalidates all but the first channel between the same iface and same task group
    auto doinvalidate = [](Operation::PortList & ports, Task* (*gettask)(const unique_ptr<Port>&))
    {
        TaskGroup * lastgroup = nullptr;
        Iface * lastiface = nullptr;
        for(auto & upport : ports)
        {
            Iface * iface = upport->GetIface();
            if(!iface) continue; //already invalidated
            
            TaskGroup * group = gettask(upport)->Group;
            if(iface == lastiface && group == lastgroup)
            {
                upport->GetChannel()->Invalidate();
            }
            else
            {
                lastiface = iface; lastgroup = group;
            }
        }        
    };
    
    static const auto fromtask = [](const unique_ptr<Port> &upp) {return upp->GetChannel()->From->GetIface()->GetTask();};
    static const auto totask = [](const unique_ptr<Port> &upp) {return upp->GetChannel()->To->GetIface()->GetTask();};
    static const auto fromposcmp = [](int pos1, int pos2) { return pos1 > pos2; };
    static const auto toposcmp = [](int pos1, int pos2) { return pos1 < pos2; };
    
    compareports fromcmp = {fromtask, fromposcmp};
    compareports tocmp = {totask, toposcmp};
    
    for(auto & op : Operations_)
    {
        std::stable_sort(op->Inputs.begin(), op->Inputs.end(), fromcmp);
        doinvalidate(op->Inputs, fromtask);

        std::stable_sort(op->Outputs.begin(), op->Outputs.end(), tocmp);
        doinvalidate(op->Outputs, totask);
    }
}

void TaskGroup::PortCleanup()
{
    for(auto & op : Operations_)
    {
        for(auto * pports : {&op->Inputs, &op->Outputs})
        {
            auto itnewend = remove_if(pports->begin(), pports->end(), [](auto & upport){return !upport->IsValid();});
            pports->erase(itnewend, pports->end());
        }
    }
}

TaskDivision::TaskDivision(TaskDivision &&other) : Groups_(std::move(other.Groups_))
{
    for(auto * pg : Groups_) pg->Division_ = this;
}

TaskDivision &TaskDivision::operator=(TaskDivision &&other)
{
    Groups_ = std::move(other.Groups_);
    for(auto *pg : Groups_) pg->Division_ = this;
    return *this;
}


bool TaskDivision::LoadStoreMembers(loadstore::LoadStore& ls)
{
    return ls.IO_Register("buffers", graph::ContainerRange<graph::PresDeque<Buffer>>(Buffers))
         & ls.IORef("groups", Groups_);
}

void TaskDivision::UpdateTasks() const
{
    Tasks_.clear();
    Tasks_.reserve(std::accumulate(Groups_.begin(), Groups_.end(), 0,
                                   [](auto n, auto * pg) { return n+pg->GetTaskCount(); }));
    
    for(auto * pg : Groups_)
    {
        for(auto & upop : pg->GetOperations())
        {
            Tasks_.push_back(upop->TheTask);
        }
    }
}

}} // namespace Ladybirds::impl
