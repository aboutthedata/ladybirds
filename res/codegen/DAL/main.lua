-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

init();

lfs.mkdir('gencode');
lfs.mkdir('gencode/DAL');
lfs.mkdir('gencode/DAL/app1');
lfs.mkdir('gencode/DAL/app1/src');

outdir=tools.realpath('gencode/DAL')..'/';



local distribute = function(n)
    local nCores = 60;
    local nHwThreads = nCores*4;

    if n % nHwThreads == nHwThreads-1 then return 0;
    else return (n*4)%(nHwThreads-1) + 1; end;
end;


-- data type checks
basetypesizes={}
for _,kernel in pairs(kernels) do
    for _,packet in ipairs(kernel.packets) do
        basetypesizes[packet.basetype] = packet.basetypesize
    end
end

-- give buffers names
for i, buffer in ipairs(buffers) do
    buffer.name = "_buffer_"..i;
end

local listbuffers = function(ports)
    local ret = "";
    for i,port in ipairs(ports) do
        if i > 1 then ret = ret..", "; end;
        ret = ret..port.iface.buffer.name;
    end
    return ret;
end

-- give groups names
for i,group in ipairs(groups) do
    local inctr, outctr = 0, 0
    group.name = "_Process"..i;
    group.inputs = {};
    group.outputs = {};
    for _,op in ipairs(group.operations) do
        op.task.group = group;
        for _,port in ipairs(op.inputs) do
            inctr = inctr+1;
            local name = "in"..inctr;
            port.name = name;
            port.macroname = string.format("PORT_%-7s", name:upper());
            group.inputs[inctr] = port;
        end
        for _,port in ipairs(op.outputs) do
            outctr = outctr+1;
            local name = "out"..outctr;
            port.name = name;
            port.macroname = string.format("PORT_%-7s", name:upper());
            group.outputs[outctr] = port;
        end
    end
end

for i,channel in ipairs(channels) do
    channel.name = "Channel"..i;
end

local bindings = {}
for i,group in ipairs(groups) do
    bindings[i] = {group = group.name, target = "core_"..distribute(i-1)};
end


--sort groups, first by number of members, then by name of first member
table.sort(groups, function(grp1, grp2) 
                       if #grp1 ~= #grp2 then return #grp1 > #grp2;
                       else return grp1.operations[1].task.name < grp2.operations[1].task.name; end
                   end);

--create view model
model = { appname=appname, ofiles=ofiles, definitions=definitions, typeckecks=map2array(basetypesizes), 
    kernels=marklast(kernels, "packets"), buffers=buffers, tasks=tasks, channels=channels, groups=groups,
    bindings=bindings, auxfiles=auxfiles};

render("configure", model);
render("fsm.xml", model)
render("mapping.xml", model)
render("app1/pn.xml", model)
render("app1/mapping1.xml", model)
render("app1/src/global.h", model)
render("app1/src/buffers.h", model)
render("app1/src/experiment.h", model)
render("app1/src/experiment.cpp", model)

process_h_template = fastache.parse(resdir.."app1/src/process.h.mustache")
process_c_template = fastache.parse(resdir.."app1/src/process.c.mustache")

outdir = outdir.."app1/src/";
groups[1].buffershere = true;
for _,group in ipairs(groups) do
    local fn = outdir..group.name..".h";
    printf("writing %s\n", fn);
    process_h_template:render(fn, group)
    fn = outdir..group.name..".c";
    printf("writing %s\n", fn);
    process_c_template:render(fn, group)
end

for _,file in ipairs(codefiles) do copy(file); end;
for _,file in ipairs(auxfiles) do copy(file); end;

-- Collect all required C files
cfiles={}
for _,task in ipairs(tasks) do
    cfiles[task.kernel.codefile] = true;
end

-- Copy all required C files and create a list of object files
ofiles = {}
for cfile in pairs(cfiles) do
    copy(cfile);
end
