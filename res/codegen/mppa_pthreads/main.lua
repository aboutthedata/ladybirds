-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

init();

lfs.mkdir('gencode');
lfs.mkdir('gencode/mppa_pthreads');
lfs.mkdir('gencode/mppa_pthreads/lb-includes');

outdir = tools.realpath('gencode/mppa_pthreads')..'/';

local bitfieldvarsize = 64


local distribute = function(n)
    local nCores = 16;    -- the MPPA-256 has 16 cores per cluster
    return n % nCores + 1;
end;

-- data type checks
basetypesizes={}
for _, kernel in pairs(kernels) do
    for _, packet in ipairs(kernel.packets) do
        basetypesizes[packet.basetype] = packet.basetypesize
    end
end

-- give buffers names
for i, buffer in ipairs(buffers) do
    buffer.name = "_buffer_"..i;
end

-- assign channel numbers
for i, channel in ipairs(channels) do
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
for i,group in ipairs(groups) do
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

taskdeps={}

--fill the task dependencies bitfield tables
for _,dep in ipairs(dependencies) do
    local src = dep.from.task;
    local deplist = dep.to.task.taskdeps;

    deplist[src.bitfieldindex] = (deplist[src.bitfieldindex] or 0) | src.bitfield;
end

-- fill bitfield output for each operation
for i,group in ipairs(groups) do
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

-- set bindings
local bindings = {}
for i, group in ipairs(groups) do
    bindings[i] = {group = group.name, target = distribute(i-1)};
end


-- Collect all required C files
cfiles={}
for _, task in ipairs(tasks) do
    cfiles[task.kernel.codefile] = true;
end

-- Copy all required C files and create a list of object files
ofiles = {"main.o", "experiment.o", "events.o", "taskmanagement.o"}
for cfile in pairs(cfiles) do
    copy(cfile);
    ofiles[#ofiles+1] = cfile:gsub('%.c$', '.o')
end

for _, file in ipairs(codefiles) do
    copy(file);
    if file:match('%.c$') then
        ofiles[#ofiles+1] = file:gsub('%.c$', '.o')
    end
end

for _, file in ipairs(auxfiles) do
    copy(file);
end


--create view model
model = { appname=appname, ofiles=ofiles, definitions=definitions, typeckecks=map2array(basetypesizes),
    kernels=marklast(kernels, "packets"), buffers=buffers, tasks=tasks, channels=channels, groups=groups,
    bindings=bindings, auxfiles=auxfiles, channelcount=#channels, threadcount=#groups, maintask=maintask,
    TaskBitfieldUnitSize=bitfieldvarsize, TaskBitfieldLength=curfieldindex};

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

for _,group in ipairs(groups) do
    local fn = outdir..group.name..".c";
    printf("writing %s\n", fn);
    thread_c_template:render(fn, group)
end

