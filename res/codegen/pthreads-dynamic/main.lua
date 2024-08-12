-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

init();
tools.mkpath('gencode/pthreads-dynamic/lb-includes');
outdir=tools.realpath('gencode/pthreads-dynamic')..'/';
local lbbase = tools.basename(args.lbfile)

local prog = Ladybirds.Parse{filename=args.lbfile, output=outdir..lbbase..'.c'};
assert(prog, nil);

local result = Ladybirds.TaskTopoSort{prog} and
        Ladybirds.CalcSuccessorMatrix{prog} and
        (not args.mapping or Ladybirds.LoadMapping{prog, filename=args.mapping}) and
        (not args.projinfo or Ladybirds.LoadProjectInfo{prog, filename=args.projinfo}) and
        Ladybirds.PopulateGroups{prog} and
        Ladybirds.BufferPreallocation{prog} and
        Ladybirds.BufferAllocation{prog} and
        true or error()

local x = Ladybirds.Export{prog};



local bitfieldvarsize = 64

if #x.divisions ~= 1 then
    error("Program has "..#x.divisions.." divisions, but only one is supported.");
end

--[[
-- Xeon Phi Mode: 60 cores, 4 threads/core
local distribute = function(n)
    local nCores = 60;
    local nHwThreads = nCores*4;

    if n % nHwThreads == nHwThreads-1 then return 0;
    else return (n*4)%(nHwThreads-1) + 1; end;
end;]]--

local distribute = function(n)
    return n%4;
end


-- data type checks
local basetypesizes={}
for _,kernel in pairs(x.kernels) do
    for _,packet in ipairs(kernel.packets) do
        basetypesizes[packet.basetype] = packet.basetypesize
    end
end

local div = x.divisions[1]

-- give buffers names
local extargs = {};
for i, buffer in ipairs(div.buffers) do
    buffer.name = "_buffer_"..i;
end

for _,buffer in ipairs(x.externalbuffers) do
    local idx = buffer.extargindex;
    buffer.name = "ExternalBuffers["..idx.."].Base"
    buffer.callparam = "ExternalBuffers["..idx.."].Dimensions"
    extargs[#extargs+1] = {index=#extargs, argname=x.maintask.kernel.packets[idx+1].name};
end

local listbuffers = function(ports)
    local ret = "";
    for i,port in ipairs(ports) do
        if i > 1 then ret = ret..", "; end;
        ret = ret..port.iface.buffer.name;
    end
    return ret;
end

for i,channel in ipairs(x.channels) do
    local n = i-1;
    channel.number = n;
    channel.from.number = n;
    channel.to.number = n;
end

local curfieldindex = 0
local nextindex = function(index, id)
    id = id+1
    if id >= bitfieldvarsize then
        return index+1, 0
    else
        return index, id
    end
end


-- give groups names and operations ids
for i,group in ipairs(x.groups) do
    group.name = "_Thread"..i;
    group.number = i-1;
    group.targetcore = distribute(i-1);
    group.localfieldindexmin = curfieldindex
    
    local curfieldid = 0
    for id,op in ipairs(group.operations) do
        op.id = id;
        op.task.group = group;
        op.task.bitfield = 1<<curfieldid
        op.task.bitfieldhex = string.format("%#x", 1<<curfieldid)
        op.task.bitfieldindex = curfieldindex
        op.task.taskdeps = {}
        
        curfieldindex, curfieldid = nextindex(curfieldindex, curfieldid)
    end
    
    group.localfieldindexmax = curfieldindex
    curfieldindex = curfieldindex+1;--start new field for new thread (only one thread writes to each field variable)
end

local taskdeps={}
x.maintask.bitfield = 0;
x.maintask.bitfieldindex = 1; --wrong index, but don't care since the bitfield is zero anyway...
x.maintask.taskdeps = {};
--fill the task dependencies bitfield tables
for _,dep in ipairs(x.dependencies) do
    local src = dep.from.task;
    local deplist = dep.to.task.taskdeps;

    deplist[src.bitfieldindex] = (deplist[src.bitfieldindex] or 0) | src.bitfield;
end


-- fill bitfield output for each operation
for i,group in ipairs(x.groups) do
    local dataoffset = 0;
    
    for _,op in ipairs(group.operations) do
        op.checkstart = dataoffset;
        
        local mydeps, mydepindices, otherdeps, otherdepindices = "", "", "", ""
        for index,data in pairs(op.task.taskdeps) do
            if index >= group.localfieldindexmin and index <= group.localfieldindexmax then
                mydepindices = mydepindices .. index .. ", ";
                mydeps = mydeps .. string.format("%#x, ", data)
            else
                otherdepindices = otherdepindices .. index .. ", ";
                otherdeps = otherdeps .. string.format("%#x, ", data)
            end
            dataoffset = dataoffset+1
        end
        
        op.depfieldindices = mydepindices .. otherdepindices;
        op.depfielddata = mydeps .. otherdeps;
        op.checkend = dataoffset;
    end
end


local bindings = {}
for i,group in ipairs(x.groups) do
    bindings[i] = {group = group.name, target = distribute(i-1)};
end




-- Copy all required C files and create a list of object files
ofiles = {"main.o", "experiment.o", "events.o", "taskmanagement.o", lbbase..'.o'}

for _,file in ipairs(x.codefiles) do
    copy(file);
    if file:match('%.c$') then
        ofiles[#ofiles+1] = file:gsub('%.c$', '.o')
    end
end

for _,file in ipairs(x.auxfiles) do
    copy(file);
end



--create view model
model = { appname=appname, ofiles=ofiles, definitions=x.definitions, typeckecks=map2array(basetypesizes), 
    kernels=x.kernels, buffers=div.buffers, tasks=x.tasks, channels=x.channels, groups=x.groups,
    bindings=bindings, auxfiles=x.auxfiles, channelcount=#x.channels, threadcount=#x.groups, maintask=x.maintask,
    TaskBitfieldUnitSize=bitfieldvarsize, TaskBitfieldLength=curfieldindex,

    MainEntryArguments=extargs, ExternalBufferCount=#extargs};

render("Makefile", model)
render("main.c", model)
render("global.h", model)
render("buffers.h", model)
render("experiment.h", model)
render("experiment.c", model)
render("events.h", model)
render("events.c", model)
render("taskmanagement.h", model)
render("taskmanagement.c", model)
render("lb-includes/ladybirds.h", model)

thread_c_template = fastache.parse(resdir.."thread.c.mustache")

for _,group in ipairs(x.groups) do
    local fn = outdir..group.name..".c";
    printf("writing %s\n", fn);
    thread_c_template:render(fn, group)
end
