-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

init();
tools.mkpath('gencode/mppa_bare/lb-includes');
tools.mkpath('gencode/mppa_bare/report');
outdir=tools.realpath('gencode/mppa_bare')..'/';
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
        true or error();

if args.stupidbanks then
    result = Ladybirds.StupidBankAssign{prog} or error();
elseif args.timings then
    result = Ladybirds.AssignBanks{prog, timingspec=args.timings} or error("error occured");
end

local x = Ladybirds.Export{prog};
assert(x, nil);


if #x.divisions ~= 1 then
    error("Program has "..#x.divisions.." divisions, but currently only one is supported.");
end

local div = x.divisions[1]


local bitfieldvarsize = 64

local distribute = function(n)
    if n < 8 then
        return (7-n) * 2;
    elseif n < 16 then
        return (15-n) * 2 + 1;
    else
        return -1;
    end
end;

local getmembank = function(number)
    local banknumber = math.floor(number / 2)
    local bankside = number % 2
    if bankside == 0 then
        bankside = "r"
    else
        bankside = "l"
    end
    return banknumber..bankside
end;

-- create a list of memory banks
local banks = {}
for i = 0,7 do
    banks[#banks+1] = {bankname=i.."r", origin=     i*128, buffers={}};
    banks[#banks+1] = {bankname=i.."l", origin=1024+i*128, buffers={}};
end

-- give buffers names and add them to the banks
for i, buffer in ipairs(div.buffers) do
    buffer.name = "_buffer_"..i;

    if buffer.membank == -1 then
        error("No memory bank has been assigned to buffer "..buffer.name..". Cannot continue...");
    end
    
    buffer.membankname = getmembank(buffer.membank);
    table.insert(banks[buffer.membank+1].buffers, buffer);
end

-- calculate the space needed for the buffers in total
for _, bank in ipairs(banks) do
    local bufferstotalsize = 0
    for _, buffer in ipairs(bank.buffers) do
        bufferstotalsize = math.max(bufferstotalsize, buffer.bankaddress+buffer.size )
    end
    bank.bufferstotalsize = bufferstotalsize
    bank.bankstart = bank.origin.."K"
end
banks[1].bankstart = "ALIGN(4K)"

-- data type checks
local basetypesizes={}
for _, kernel in pairs(x.kernels) do
    for _, packet in ipairs(kernel.packets) do
        basetypesizes[packet.basetype] = packet.basetypesize
    end
end

-- assign channel numbers
for i, channel in ipairs(x.channels) do
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

local idlecores = 0xffff;

-- give groups names and operations ids
for i,group in ipairs(x.groups) do
    group.mappingname = group.name;
    group.name = "_Thread"..i;
    group.number = i-1;
    group.targetcore = distribute(i-1);
    idlecores = idlecores & (~(1 << group.targetcore));
    group.membankname = getmembank(group.targetcore);
    group.localfieldindexmin = curfieldindex
    group.numoperations = #group.operations

    local curfieldid = 0
    for id,op in ipairs(group.operations) do
        op.id = id;
        op.task.group = group;
        op.task.bitfield = 1<<curfieldid
        op.task.bitfieldhex = string.format("%#x", 1<<curfieldid)
        op.task.bitfieldindex = curfieldindex
        op.task.taskdeps = {}
        op.task.notifies = 0

        curfieldindex, curfieldid = nextindex(curfieldindex, curfieldid)
    end

    group.localfieldindexmax = curfieldindex
    curfieldindex = curfieldindex+1;--start new field for new thread (only one thread writes to each field variable)
end

--fill the task dependencies bitfield tables
local taskdeps={}
x.maintask.bitfield = 0;
x.maintask.bitfieldindex = 1; --wrong index, but don't care since the bitfield is zero anyway...
x.maintask.taskdeps = {};
x.maintask.group = {targetcore=-1};
x.maintask.notifies = 0;
for _,dep in ipairs(x.dependencies) do
    local src = dep.from.task;
    local dst = dep.to.task;
    local deplist = dst.taskdeps;

    deplist[src.bitfieldindex] = (deplist[src.bitfieldindex] or 0) | src.bitfield;
    
    local srccore, dstcore = src.group.targetcore, dst.group.targetcore;
    if srccore ~= dstcore then src.notifies = src.notifies | (1<<dstcore); end
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
        op.notifies = string.format("%#x", op.task.notifies);
    end
end

-- set bindings
local bindings = {}
for i, group in ipairs(x.groups) do
    bindings[i] = {group = group.name, target = distribute(i-1)};
end


local HandleLBCode = function(file)
    local f = io.open(file)
    local content = f:read("*a")
    content = string.gsub(content, '[^%w_]fromfile%s*%(([^,]+)%s*,%s*([^,]+)%s*,%s*"([^"]*)"%s*%)',
        function(target,size,ifile)
            local f = io.open(indir..ifile)
            assert(f, "Could not open "..indir..ifile);
            local bin = f:read("*a")
            local hex = string.gsub(bin, "(.)", function(c) return string.format('\\x%02x',string.byte(c)) end)
            local s = string.format('{memcpy(%s, "%s", %s); _Static_assert((%s)==%d, "input file size mismatch");}', target, hex, size, size, #bin);
            --print(s);
            return s;
        end);
    f:close()
    io.open(file, "w"):write(content)
end


-- Collect all required C files
local cfiles = {"cluster.c", "noc.c", "taskmanagement.c", lbbase..".c"}
HandleLBCode(outdir..lbbase..".c")

for _, file in ipairs(x.codefiles) do
    copy(file);
    if file:match('%.c$') then
        cfiles[#cfiles+1] = file;
    end
end

for _, file in ipairs(x.auxfiles) do
    copy(file);
end

printreport(x);
if #x.groups > 16 then
    error("Too many groups. Only 16 threads are supported.");
end


--create view model
model = { membanks=banks, appname=appname, cfiles=cfiles, definitions=x.definitions, typeckecks=map2array(basetypesizes),
    kernels=x.kernels, buffers=div.buffers, tasks=x.tasks, channels=x.channels, groups=x.groups, bindings=bindings,
    auxfiles=x.auxfiles, channelcount=#x.channels, threadcount=#x.groups, taskcount = #x.tasks, maintask=x.maintask,
    TaskBitfieldUnitSize=bitfieldvarsize, TaskBitfieldLength=curfieldindex, idlecores=string.format("%#x", idlecores)};

render("Makefile", model)
render("buffers.ld", model) -- simple list of buffer symbols for interleaved mode
render("linker.ld", model)   -- complete linker script with buffers and their locations for sequential mode
render("io.c", model)
render("cluster.c", model)
render("global.h", model)
render("buffers.h", model)
render("noc.h", model)
render("noc.c", model)
render("host.c", model)
render("taskmanagement.h", model)
render("taskmanagement.c", model)
render("report/mapping.lua", model)
render("lb-includes/ladybirds.h", model)

thread_c_template = fastache.parse(resdir.."thread.c.mustache")

for _,group in ipairs(x.groups) do
    local fn = outdir..group.name..".c";
    printf("writing %s\n", fn);
    thread_c_template:render(fn, group);
end
