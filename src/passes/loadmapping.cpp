// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include <memory>
#include <string>
#include <unordered_map>

#include "opt/bankassignment.h"
#include "opt/cacheindexopt.h"
#include "lua/luaenv.h"
#include "lua/luaload.h"
#include "lua/pass.h"
#include "spec/platform.h"
#include "loadstore.h"
#include "msgui.h"
#include "program.h"
#include "task.h"
#include "taskgroup.h"


using std::string;
using Ladybirds::spec::Task;
using Ladybirds::spec::Platform;

struct MappingArgs : public Ladybirds::loadstore::LoadStorableCompound
{
    std::string Filename;
    Platform *pPlatform = nullptr;
    virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override
    {
        return ls.IO("filename", Filename) & ls.IOHandle("platform", pPlatform, nullptr, false);
    }
};

bool LoadMapping(Ladybirds::impl::Program &prog, MappingArgs & args);

Ladybirds::lua::PassWithArgs<MappingArgs> LoadMappingPass("LoadMapping", &LoadMapping);

bool LoadMapping(Ladybirds::impl::Program &prog, MappingArgs & args)
{
    Ladybirds::lua::LuaEnv lua;
    
    /* source mapping file and do some preparation already in lua:
     * Pack all tasks of a group together into one list. Then, sort the list by group name.
     * The sorting is necessary because the order of elements in lua tables without numerical index is random and will
     * vary from execution to execution. This would introduce non-deterministic behaviour which is undesired in this
     * framework (e.g. when debugging or with an execution feedback algorithm that is supposed to converge.
     * Sorting is done directly in lua, because this is easier and groups are correctly sorted even when the group name
     * is an integer. */
    if(!lua.DoFile(args.Filename.c_str()) || !lua.DoString(R"(
            if grouping == nil then error("Mapping specification doesn't define 'grouping' table"); end
                                                              
            Groups = {};
            local groupmap = {};
            for taskname,groupname in pairs(grouping) do
                local group = groupmap[groupname];
                if group == nil then
                    group = {name=groupname, tasks={}};
                    groupmap[groupname] = group;
                    Groups[#Groups+1] = group;
                end
                group.tasks[#group.tasks+1] = taskname;
            end
            
            table.sort(Groups, function(a,b) return a.name < b.name; end);
        )", "while processing the mapping file")) return false;

    
    //now load the previously prepared lua description into C++ classes
    struct groupdesc : public Ladybirds::loadstore::LoadStorableCompound
    {
        std::string name;
        std::vector<std::string> tasks;
        virtual bool LoadStoreMembers(Ladybirds::loadstore::LoadStore & ls) override
            {return ls.IO("name", name) & ls.IO("tasks", tasks);}
    };
        
    Ladybirds::lua::LuaLoad load(lua);
    lua_pushglobaltable(lua);

    std::vector<groupdesc> groupdescs;
    std::vector<std::vector<std::string>> divdescs;
    if(!load.IO("Groups", groupdescs) ||
        !load.IO("divisions", divdescs, false))
        return false;
    
    //create a name to task index dictionary
    std::unordered_map<string, Task*> tasks; //task indices
    for(auto & t : prog.GetTasks())
    {
        auto it = tasks.emplace(t.Name, &t);
        if(!it.second) gMsgUI.Warning("Ambiguous task name: %s", t.Name.c_str());
    }
    
    std::unordered_map<std::string, Ladybirds::impl::TaskGroup *> groups;
    
    //finally, create the groups
    int id = 0;
    for(auto & gd : groupdescs)
    {
        //first of all, convert task names to real tasks using our dictionary
        std::vector<Task*> grouptasks;
        grouptasks.reserve(gd.tasks.size());
        for(auto & s : gd.tasks)
        {
            auto it = tasks.find(s);
            if(it != tasks.end()) grouptasks.push_back(it->second);
            else gMsgUI.Warning("Task '%s', as specified in grouping table, does not exist", s.c_str());
        }
        if(grouptasks.empty()) continue;
        
        //again, eliminate any randomness from lua description
        //sort by indices, which are ultimately determined by the source code
        std::sort(grouptasks.begin(), grouptasks.end(), 
                  [](Task * pt1, Task * pt2) { return pt1->GetID() < pt2->GetID();});
        
        //store data as Ladybirds group structure
        auto upgroup = std::make_unique<Ladybirds::impl::TaskGroup>(id++, gd.name);
        for(auto * ptask : grouptasks)
        {
            ptask->Group = upgroup.get();
            upgroup->AddTask(ptask);
        }
        groups[gd.name] = upgroup.get();
        prog.Groups.push_back(std::move(upgroup));
    }
    
    for(auto &t : prog.GetTasks())
    {
        if(!t.Group) gMsgUI.Warning("Task '%s' is not included in the mapping file", t.GetFullName().c_str());
    }
    
    prog.Divisions.reserve(prog.Divisions.size()+divdescs.size()+1);
    for(auto & dd : divdescs)
    {
        prog.Divisions.emplace_back();
        auto & div = prog.Divisions.back();
        for(auto & gname : dd)
        {
            auto it = groups.find(gname);
            if(it == groups.end())
            {
                gMsgUI.Warning("Group '%s', as specified in division table, does not exist", gname.c_str());
                continue;
            }
        
            if(it->second->GetDivision())
            {
                gMsgUI.Error("Trying to assign group '%s' to multiple divisions", gname.c_str());
                continue;
            }
            
            div.AddGroup(it->second);
        }
    }
    
    bool ret = true;
    if(args.pPlatform)
    {
        std::unordered_map<string, const Platform::Core*> cores;
        for(auto &c : args.pPlatform->GetCores()) cores[c.Name] = &c;
        for(auto &upg : prog.Groups)
        {
            auto it = cores.find(upg->GetName());
            if(it != cores.end())
            {
                upg->Bind(it->second);
                // Note: Because of the input format, there can be no two groups with the same name,
                // so we do not need to check if two groups could be assigned to the same core
            }
            else
            {
                gMsgUI.Error("Processing element '%s', as specified in the given binding, "
                             "does not exist in the platform", upg->GetName().c_str());
                ret = false;
            }
        }
    }
    
    return ret;
}
