-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

init();
tools.mkpath('gencode/graphviz');
outdir=tools.realpath('gencode/graphviz')..'/';

local prog = Ladybirds.Parse({filename=args.lbfile});
assert(prog, nil);
local x = Ladybirds.Export(prog);

local dotfiles = {}
local mk_dot_template = fastache.parse(resdir.."metakernel.dot.mustache");

local outputs = {name="[program outputs]", ifaces=x.maintask.ifaces}
for _,iface in ipairs(x.maintask.ifaces) do
    outputs[#outputs+1] = {name=iface.name}
end
x.maintask.name="[program inputs]"
for _,dep in ipairs(x.dependencies) do
    if dep.to.task == x.maintask then dep.to.task = outputs; end
end

local fullprog = { name="full program", tasks = x.tasks, dependencies = x.dependencies,
                   inputs = x.maintask, outputs = outputs};
table.insert(x.metakernels, fullprog);

for _,mk in pairs(x.metakernels) do
    printf("now processing %s\n", mk.name);
    local tasksmodel = {};
    for _,task in ipairs(mk.tasks) do
        local ports = {["in"] = {}, out = {}, inout = {}};
        local portsin, portsout, portsinout = ports["in"], ports.out;
        
        for _,iface in ipairs(task.ifaces) do
            local p = iface.packet;
            table.insert(ports[p.dir], {valid=true, name=p.name, dims=p.arraydims});
        end
        
        local fill, fillcnt;
        if #portsin > #portsout then
            fill, fillcnt = portsout, #portsin;
        else
            fill, fillcnt = portsin, #portsout;
        end
        
        local dummy = { name = false; }
        for i = #fill+1,fillcnt do
            fill[i] = dummy;
        end
        
        local taskmodel = {name=task.name, onecol={}, twocol={}};
        for i = 1,fillcnt do
            taskmodel.twocol[i] = {ports = {portsin[i], portsout[i]}};
        end
        for _,port in ipairs(ports.inout) do
            taskmodel.onecol[#taskmodel.onecol+1] = port;
        end
        
        tasksmodel[#tasksmodel+1] = taskmodel;
    end
    
    local inputs, outputs = {}, {}
    for _,iface in ipairs(mk.inputs.ifaces) do
        local p = iface.packet;
        if p.dir ~= "out" then inputs[#inputs+1] = {name=p.name, dims=p.arraydims}; end
    end
    for _,iface in ipairs(mk.outputs.ifaces) do
        local p = iface.packet;
        if p.dir ~= "in" then outputs[#outputs+1] = {name=p.name, dims=p.arraydims}; end
    end
    
    for _,dep in ipairs(mk.dependencies) do
        dep.from.indexstring = indexstring(dep.from.index);
        dep.to.indexstring = indexstring(dep.to.index);
    end
    
    local arguments = {};
    if #inputs > 0 then arguments[1] = {name=mk.inputs.name,packets=inputs}; end;
    if #outputs > 0 then arguments[#arguments+1] = {name=mk.outputs.name,packets=outputs}; end;
    local model = {name = mk.name, tasks=tasksmodel, arguments=arguments, dependencies=mk.dependencies};
    
    local filename = string.gsub(mk.name, " ", "-")..'.dot';
    
    mk_dot_template:render(outdir..filename, model);
    dotfiles[#dotfiles+1] = filename;
end

model = { appname=appname, dotfiles=dotfiles};
render("Makefile", model)
