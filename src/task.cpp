// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "task.h"
#include <string>
#include <sstream>

#include "kernel.h"
#include "tools.h"
#include "buffer.h"

namespace Ladybirds { namespace spec {


const std::string &Iface::GetName() const
{
    return Packet_->GetName();
}

std::string Iface::GetFullName() const
{
    if(!reinterpret_cast<const void*>(this)) return "<null iface>";
    return (Task_ ? Task_->GetFullName() : "<null>") + '.' + GetName();
}

std::ostream & operator<<(std::ostream &strm, Iface &iface)
{
    return strm << iface.GetFullName();
}

Iface::Iface(Task * ptask, const Iface & other)
    : Task_(ptask), Packet_(other.Packet_), Dimensions_(other.Dimensions_), 
      PosHint(other.PosHint), BufferHint(other.BufferHint)
{
}

int Iface::GetMemSize() const
{
    return Product(Dimensions_) * Packet_->GetBaseType().Size;
}

Iface::BuddyList Iface::GetBuddies() const
{
    auto &packetbuddies = Packet_->GetBuddies();
    BuddyList ret; ret.reserve(packetbuddies.size());
    for(auto &d : GetTask()->Ifaces)
    {
        if(packetbuddies.count(d.GetPacket())) ret.push_back(&d);
    }
    return ret;
}

bool Iface::LoadStoreMembers(loadstore::LoadStore& ls)
{
    std::string callparam = "(int[]){";
    if(ls.IsStoring())
    {
        if(!BufferDimsAdj_.empty())
        {
            for(auto i = BufferDimsAdj_.size(); --i > 0; ) (callparam += std::to_string(BufferDimsAdj_[i])) += ", ";
            (callparam += std::to_string(BufferDimsAdj_[0])) += '}';
        }
        else callparam += "0}";
    }
    return ls.IORef("task", Task_)
         & ls.IORef("packet", Packet_)
         & ls.IORef("buffer", Buffer_, false)
         & ls.IO("offset", BufferOffset_)
         & ls.IO("bufferdims", BufferDimsAdj_)
         & ls.IO("callparam", callparam, false);
}

Task::Task ( const Task& other )
 : Kernel_(other.Kernel_), Params_(other.Params_), DerivedParams_(other.DerivedParams_), Name(other.Name)
{
    Ifaces.reserve(Kernel_->Packets.size());
    for(auto & oiface : other.Ifaces)
    {
        Ifaces.push_back(Iface(this, oiface));
    }
}

Task::Task (Task&& other)
: basenode(std::move(other)),
  Kernel_(other.Kernel_), Params_(std::move(other.Params_)), DerivedParams_(std::move(other.DerivedParams_)),
  Name(std::move(other.Name)), Cost(other.Cost), Ifaces(std::move(other.Ifaces))
{
    for(auto & iface : Ifaces) iface.Task_ = this;
}

Task& Task::operator= ( Task&& other )
{
    Kernel_ = other.Kernel_;
    Name = std::move(other.Name);
    Params_ = std::move(other.Params_);
    DerivedParams_ = std::move(other.DerivedParams_);
    Ifaces = std::move(other.Ifaces);
    for(auto & iface : Ifaces) iface.Task_ = this;
    Cost = other.Cost;
    return *this;
}


bool Task::LoadStoreMembers(loadstore::LoadStore& ls)
{
    if(!(ls.IORef("kernel", Kernel_)
         & ls.IO("name", Name)
         & ls.IO("parameters", Params_, false)
         & ls.IO("derivedparams", DerivedParams_, false))) return false;
    
    if(ls.IsLoading())
    {
        assert(false && "not supported");
        //FillIfaces();
        return true;
    }
    else 
    {
        return ls.IO_Register("ifaces", Ifaces);
    }
}

std::string Task::GetFullName() const
{
    return Name;
}

void Task::FillIfaces()
{
    assert(Ifaces.empty());
    
    Ifaces.reserve(Kernel_->Packets.size());
    for(auto & packet : Kernel_->Packets)
    {
        auto dims = packet.GetArrayDims();
        for(auto & dim : dims) if(dim < 0) dim = DerivedParams_[-dim-1];
        Ifaces.emplace_back(this, &packet, dims);
    }
}

Iface* Task::GetIfaceByName(const std::string& name)
{
    return &*std::find_if(Ifaces.begin(), Ifaces.end(),
        [&name](auto & iface) {return iface.GetName() == name;});
}

}}//namespace Ladybirds::spec

