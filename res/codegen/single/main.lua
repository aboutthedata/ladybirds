-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

init();
tools.mkpath('gencode/single/lb-includes');
outdir=tools.realpath('gencode/single')..'/';
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
migratefromglobal(x)

if #x.divisions ~= 1 then
    error("Program has "..#x.divisions.." divisions, but only one is supported.");
end

local div = x.divisions[1]

-- Collect all required C files
local cfiles={}
for _,task in ipairs(x.tasks) do
    cfiles[task.kernel.codefile] = task.kernel.source;
end

-- Copy all required C files and create a list of object files
local ofiles = {"experiment.o", lbbase..'.o'}

for _,file in ipairs(x.codefiles) do
    copy(file);
    if file:match('%.c$') then
        ofiles[#ofiles+1] = file:gsub('%.c$', '.o')
    end
end

for _,file in ipairs(x.auxfiles) do
    copy(file);
end

-- data type checks
local basetypesizes={}
for _,kernel in pairs(x.kernels) do
    for _,packet in ipairs(kernel.packets) do
        basetypesizes[packet.basetype] = packet.basetypesize
    end
end

-- give buffers names
for i, buffer in ipairs(div.buffers) do
    if buffer.isexternal then
        local idx = buffer.extargindex;
        local argname = x.maintask.kernel.packets[idx+1].name;
        buffer.name = "_lb_base_"..argname;
        buffer.callparam = "_lb_size_"..argname;
    else
        buffer.name = "_buffer_"..i;
    end
end


--create view model
model = { appname=appname, ofiles=ofiles, definitions=x.definitions, typeckecks=map2array(basetypesizes), 
    kernels=x.kernels, buffers=x.buffers, tasks=x.tasks, maintask=x.maintask };



render("Makefile", model)
render("global.h", model)
render("main.c", model)
render("lb-includes/ladybirds.h", model)
render("experiment.h", model)
render("experiment.c", model)


