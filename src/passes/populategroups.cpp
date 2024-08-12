// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "lua/pass.h"
#include "program.h"
#include "task.h"
#include "taskgroup.h"

namespace {
using namespace Ladybirds::spec;
using Ladybirds::impl::Program;
using Ladybirds::impl::TaskGroup;
using Ladybirds::impl::Channel;

bool PopulateGroups(Program &prog);
Ladybirds::lua::Pass PopulateGroupsPass("PopulateGroups", &PopulateGroups);

// Ensures that every task is a member of a group.
// For each task that does not yet belong to a group, creates a new group with this task as only member
void CreateTrivialGroups(Ladybirds::spec::TaskGraph & tg, Program::GroupList & groups)
{
    auto nfreetasks = std::count_if(tg.NodesBegin(), tg.NodesEnd(), [](auto & t){ return !t.Group; });
    if(!nfreetasks) return;
    
    groups.reserve(groups.size()+nfreetasks);
    for(auto & t : tg.Nodes())
    {
        if(!t.Group) groups.push_back(std::make_unique<TaskGroup>(&t));
    }
}

// Ensures that every group is a of a division.
// If necessary, creates a new division to hold all groups which have no division yet.
void CreateTrivialDivision(Program::GroupList & groups, Program::DivisionList & divs)
{
    auto nfreegroups = std::count_if(groups.begin(), groups.end(), [](auto & upgrp){ return !upgrp->GetDivision(); });
    if(!nfreegroups) return;
    
    divs.emplace_back();
    auto & div = divs.back();
    div.ReserveForMoreGroups(nfreegroups);
    for(auto & upgrp : groups)
    {
        if(!upgrp->GetDivision()) div.AddGroup(upgrp.get());
    }
}

// Fills the Inputs/Outputs data members of the operations in the groups according to the information given in the routes
void PopulateInputsOutputs(Program::GroupList & groups, Program::DepList & deps, Program::ChannelList & channels)
{
    for(auto & dep : deps)
    {
        Iface *fromiface = dep.From.TheIface, *toiface = dep.To.TheIface;
        Task *fromtask = fromiface->GetTask(), *totask = toiface->GetTask();
        TaskGroup *fromgroup = fromtask->Group, *togroup = totask->Group;
        if(fromgroup == togroup)
            continue; //block will be passed on internally; no need for creating an external interface
        if(!fromgroup || !togroup)
            continue; //Don't insert channels for main invoke inputs/outputs. TODO: See if this needs to change
        
        auto * fromport = fromgroup->AddOutputPort(dep);
        auto * toport = togroup->AddInputPort(dep);
        channels.emplace_back(std::make_unique<Channel>(fromport, toport, &dep));
    }
}

// Removes superfluent ports and channels
void SimplifyChannels(Program::GroupList & groups, Program::ChannelList & channels)
{
    for(auto & upgroup : groups) upgroup->SimplifyPorts();
    auto itnewend = std::remove_if(channels.begin(), channels.end(), [](auto &upchan){return !upchan->IsValid();});
    channels.erase(itnewend, channels.end());
    for(auto & upgroup : groups) upgroup->PortCleanup();
}


/// Makes sure each task is part of a group, creating a new group for each non-grouped task.
/// Also ensures that each group is a member of a division, creating one single additional division if necessary.
bool PopulateGroups(Program &prog)
{
    CreateTrivialGroups(prog.TaskGraph, prog.Groups);
    CreateTrivialDivision(prog.Groups, prog.Divisions);
    PopulateInputsOutputs(prog.Groups, prog.Dependencies, prog.Channels); //TODO: take into account also false dependencies
    //SimplifyChannels(prog.Groups, prog.Channels);
    // TODO: Channel "simplification" was only used for DAL; now commented out. Check if and where it should be
    // reintroduced in some other place.
    return true;
}

} //namespace ::
