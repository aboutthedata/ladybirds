-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

init();

tools.mkpath('gencode/LPC54102/Files_M4/src');
tools.mkpath('gencode/LPC54102/Files_M0/src');
outdir=tools.realpath('gencode/LPC54102')..'/';

local prog = Ladybirds.Parse{filename=args.lbfile, output=outdir.."Files_M4/src/ladybird_kernels.c"};
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



local distribute = function(n)
    local nCores = 60;
    local nHwThreads = nCores*4;

    if n % nHwThreads == nHwThreads-1 then return 0;
    else return (n*4)%(nHwThreads-1) + 1; end;
end;



-- calculte the vector length for bits with blocks of minLength
local bitvectorsize = function(numberOfTasks, minLength) 
  bitremainder = 0;
  if(numberOfTasks%minLength>0) then
    bitremainder=1;
  end
  vecSize = math.floor(numberOfTasks/minLength) + bitremainder;
  --print(numberOfTasks);
  --print(vecSize);
  return vecSize;
end;


--print("\n\n -------------------------------- TASKS -------------------------------- \n\n");

-- add global index to task-list
--get number of tasks
numberOfTasks = 0;
for i,task in pairs(x.tasks) do
	task.task_index_global = i-1;
    numberOfTasks = numberOfTasks + 1
end



-- data type checks
basetypesizes={}
for _,kernel in pairs(x.kernels) do
    for _,packet in ipairs(kernel.packets) do
        basetypesizes[packet.basetype] = packet.basetypesize
    end
end


-- give buffers names
for i, buffer in ipairs(div.buffers) do
    buffer.name = "_buffer_"..i;
    buffer.index_in_array = i-1;
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


for i,group in ipairs(x.groups) do
    --group.name = "_Thread"..i;
    group.number = i-1;
    group.targetcore = distribute(i-1);
    for _,op in ipairs(group.operations) do
        op.task.group = group;
    end
end



local bindings = {}
for i,group in ipairs(x.groups) do
    bindings[i] = {group = group.name, target = distribute(i-1)};
end

dependency_matrix = {}

--make rows:
for i=1, numberOfTasks, 1 do
   dependency_matrix[i]={};
end

--fill matrix with zeros
for i=1, numberOfTasks, 1 do
   for j=1, numberOfTasks, 1 do
      dependency_matrix[i][j]=0;
   end
end

--fill in the dependencies
for i,dep in ipairs(x.dependencies) do
   local from_task = dep.from.task.task_index_global;
   local to_task = dep.to.task.task_index_global;
   dependency_matrix[to_task+1][from_task+1]=1;  -- index shift because task_index_global start counting with "0"
end


--bring dependency matrix in form usuable for template
dependency_matrix_mustache = {};
for i=1, numberOfTasks, 1 do
   table.insert(dependency_matrix_mustache, i, {row = {}});
   --inner list
   for k=1, numberOfTasks, 1 do
	table.insert(dependency_matrix_mustache[i].row, k, {number = dependency_matrix[i][k]});
   end
end


--checks, if an element was already existed to a list and returns; true: was already inserted, false: not yet inserted
function checkIfInserted(task_list, number)
	for _,taskNr in ipairs(task_list) do
		if (taskNr.from_task == number) then
			return true;
		end
	end
	return false;
end


dependency_struct = {}

--insert for each task a table
for i=1, numberOfTasks, 1 do
	table.insert(dependency_struct, {counter=0, tasks ={}});
end

--fill in the dependencies
for i,dep in ipairs(x.dependencies) do
   local from_task = dep.from.task.task_index_global;
   local to_task = dep.to.task.task_index_global;
   --insert only if not yet inserted
   if(checkIfInserted(dependency_struct[to_task+1].tasks, from_task) == false) then
      dependency_struct[to_task+1].counter = dependency_struct[to_task+1].counter+1;
      table.insert(dependency_struct[to_task+1].tasks, {from_task = from_task});
   end
end

for i=1, numberOfTasks, 1 do
	if(dependency_struct[i].counter == 0) then
		dependency_struct[i].no_dependencies = true;
	else
		dependency_struct[i].no_dependencies = false;
	end
	dependency_struct[i].index = i-1;
end


--generate array with all task names and indexes
task_table = {};

tasks_execution_order = {};
for key,task in ipairs(x.tasks) do
	task_table[key]={kernel = task.kernel.name, index = task.task_index_global, done = false};
end



while_flag=true;
while (while_flag) do
--go through all tasks
for key,task in ipairs(task_table) do
	--is this task still to do?
	if(task.done == false) then
		dependenciesSatisfied = true;
		for key2,task2 in ipairs(task_table) do
			if(dependency_matrix[task.index+1][task2.index+1] == 1) then
				--is the task, on which the possible new one is dependent, already executed?
				if(task2.done == false) then
					dependenciesSatisfied = false;
				end
			end
		end

		if(dependenciesSatisfied == true) then
			task.done = true;
			table.insert(tasks_execution_order, {name = task.kernel});
			
		end
	end
	
	--are all tasks done?
	alltasksDone = true;
	for key3,task3 in ipairs(task_table) do
		if (task3.done == false) then
			alltasksDone = false;
		end
	end

	if(alltasksDone == true) then
		while_flag = false;
		break;
	end

end
end


mapping_vector = {}

for i=1, numberOfTasks, 1 do
   mapping_vector[i]=0;
end


for i,group in ipairs(x.groups) do
   if(group.name == "m4") then
      for k,operation in ipairs(group.operations) do
   	   local task_index = operation.task.task_index_global+1;
	   mapping_vector[task_index] = 4;
      end
   end
end

--bring mapping vector to a form usuable for the template
mapping_vector_mustache = {};
for i=1, numberOfTasks, 1 do
   table.insert(mapping_vector_mustache, i, {number = mapping_vector[i]});
end



---------- Mapping Vector as Bit Representation 1=> M4  0=> M0 ---------------------
byteBit = 0
mapping_vector_bytepackets_bitrepresentation_mustache = {}
bitcounter = 0
minPacksize = 8;
bytecounter = 0
for i=1, numberOfTasks, 1 do
  if(mapping_vector_mustache[i].number>0) then
     byteBit = byteBit | (0x1 << bitcounter);
  end
     bitcounter = bitcounter +1 ;   
     bitcounter = bitcounter%minPacksize
  
  if(bitcounter == 0) then
    bytecounter = bytecounter +1;
    table.insert(mapping_vector_bytepackets_bitrepresentation_mustache, bytecounter, {number =byteBit});
    byteBit = 0;
  end
end
if(numberOfTasks%minPacksize>0) then
   table.insert(mapping_vector_bytepackets_bitrepresentation_mustache, bytecounter+1, {number = byteBit});
end

--make a list, which tasks are mapped to the M4

--get the correct groups for M4 and M0
printreport(x);
if #x.groups > 2 then
    error("Too many groups. Only 2 threads are supported.");
end


function assigngroups(g1, g2)
    if     g1.name == "m4" then return g1, g2
    elseif g1.name == "m0" then return g2, g1
    elseif g2 == nil then
        printf("Assigning only group '%s' to M4\n", g1.name);
        return g1, nil;
    elseif g2.name == "m0" then return g1, g2
    elseif g2.name == "m4" then return g2, g1
    else
        printf("Assigning group '%s' to M4 and group '%s' to M0\n", g1.name, g2.name);
        return g1, g2;
    end
end

local group_tasksM4, group_tasksM0 = assigngroups(table.unpack(x.groups))
local group_tasksM0 = group_tasksM0 or {operations={}}


--get all the indexes of the tasks that belong to the M4
local tasklist_m4 = {};
if (group_tasksM4 ~= nil) then
	for i,operation in ipairs(group_tasksM4.operations) do
	    tasklist_m4[operation.task.task_index_global] = true;
	end
end


-- prepare the functionpointers as output in mustache
functionpointers_m4_mustache = {};
for i=1, numberOfTasks, 1 do
   if(tasklist_m4[i-1] == nil) then
	table.insert(functionpointers_m4_mustache, i, {function_pointer = "NULL"});
   else
	table.insert(functionpointers_m4_mustache, i, {function_pointer = "&function_"..i-1});
   end
end




--make a list, which tasks are mapped to the M0
tasklist_m0 = {};
if (group_tasksM0 ~= nil) then
	for i,operation in ipairs(group_tasksM0.operations) do
	    tasklist_m0[operation.task.task_index_global] = true;
	end
end


-- prepare the functionpointers as output in mustache
functionpointers_m0_mustache = {};
for i=1, numberOfTasks, 1 do
   if(tasklist_m0[i-1] == nil) then
	table.insert(functionpointers_m0_mustache, i, {function_pointer = "NULL"});
   else
	table.insert(functionpointers_m0_mustache, i, {function_pointer = "&function_"..i-1});
   end
end



------------------------------------------------ Collect all required C files --------------------------------

-- Copy all required C files and create a list of object files
ofiles = {"main.o", "experiment.o", "ladybird_kernels.c"}

copy(outdir.."Files_M4/src/ladybird_kernels.c", outdir.."Files_M0/src/ladybird_kernels.c")
for _,file in ipairs(x.codefiles) do
    copy(indir..file, outdir.."Files_M4/src/"..file);
    copy(indir..file, outdir.."Files_M0/src/"..file);
    if file:match('%.c$') then
        ofiles[#ofiles+1] = file:gsub('%.c$', '.o')
    end
end

for _,file in ipairs(x.auxfiles) do
    copy(indir..file, outdir.."Files_M4/src/"..file);
    copy(indir..file, outdir.."Files_M0/src/"..file);
end

--create view model
model = { appname=appname, ofiles=ofiles, definitions=x.definitions, typeckecks=map2array(basetypesizes), 
    kernels=x.kernels, buffers=div.buffers, tasks=x.tasks, channels=x.channels, groups=x.groups, groups1=x.groups,
    bindings=bindings, auxfiles=x.auxfiles, channelcount=#x.channels, threadcount=#x.groups};



--render file for mapping specification
model_mapping ={tasks = x.tasks};
render("mapping.lua", model_mapping)

--render file for external files
filecopy(resdir.."external_files.lua", outdir.."external_files.lua");

-- render header-file for kernels
kernels_lpc_h_template = fastache.parse(resdir.."kernels_lpc.h.mustache")
kernels_lpc_h_template:render(outdir.."Files_M4/src/ladybird_kernels.h", model)
kernels_lpc_h_template:render(outdir.."Files_M0/src/ladybird_kernels.h", model)

local outdirbase = outdir;

-- render "AutomaticGeneratedVariables" for M4
outdir = outdirbase.."Files_M4/src/";

model_M4_autoGenVar_c = {matrix = dependency_matrix_mustache, vector = mapping_vector_mustache, vectorByte = mapping_vector_bytepackets_bitrepresentation_mustache,taskcount = numberOfTasks, vecsize = bitvectorsize(numberOfTasks,8)};
render("AutomaticGeneratedVariables.c", model_M4_autoGenVar_c)
model_M4_autoGenVar_h = {taskcount = numberOfTasks, vecsize = bitvectorsize(numberOfTasks,8)};
render("AutomaticGeneratedVariables.h", model_M4_autoGenVar_h)



--render small dependency struct
model_dependency_struct = {taskcount = numberOfTasks, vecsize = bitvectorsize(numberOfTasks,8), struct = dependency_struct, struct2 = dependency_struct};
render("dependency_structure.c", model_dependency_struct)
render("dependency_structure.h", model_dependency_struct)



--render the functions and the function-pointer-arrays

--for M4:
model_M4 = {g=group_tasksM4, f_ptr = functionpointers_m4_mustache, taskcount = numberOfTasks, vecsize = bitvectorsize(numberOfTasks,8)};
render("Functionpointers_M4.c", model_M4)
render("Functionpointers_M4.h", model_M4)

--for M0:
outdir = outdirbase.."Files_M0/src/";

model_M0 = {g=group_tasksM0, f_ptr = functionpointers_m0_mustache, taskcount = numberOfTasks, vecsize = bitvectorsize(numberOfTasks,8)};
render("Functionpointers_M0.c", model_M0)
render("Functionpointers_M0.h", model_M0)



--render buffers
outdir = outdirbase.."Files_M4/src/";
model_buffers = {t1 = div.buffers, t2 = div.buffers, trollycount = #div.buffers}
render("buffers.c", model_buffers)
render("buffers.h", model_buffers)


--render task-execution-order file
outdir = outdirbase;
model_taskExecutionOrder = {tasks = tasks_execution_order, taskcount = numberOfTasks, vecsize = bitvectorsize(numberOfTasks,8)}
render("orderTasksOneCore.txt", model_taskExecutionOrder)

--render dependency-matrix for lua script
model_depMatrix = {matrix = dependency_matrix_mustache}
render("dependency_matrix.lua", model_depMatrix)




--copy the standard files
--for M0:
filecopy(resdir.."standard_M0/crp.c", outdir.."Files_M0/src/crp.c");
filecopy(resdir.."standard_M0/cr_startup_lpc5410x-m0.c", outdir.."Files_M0/src/cr_startup_lpc5410x-m0.c");
filecopy(resdir.."standard_M0/ladybirds.h", outdir.."Files_M0/src/ladybirds.h");
filecopy(resdir.."standard_M0/m0slave_blinky.c", outdir.."Files_M0/src/m0slave_blinky.c");
filecopy(resdir.."standard_M0/sysinit.c", outdir.."Files_M0/src/sysinit.c");

--for M4:
filecopy(resdir.."standard_M4/crp.c", outdir.."Files_M4/src/crp.c");
filecopy(resdir.."standard_M4/cr_startup_lpc5410x.c", outdir.."Files_M4/src/cr_startup_lpc5410x.c");
filecopy(resdir.."standard_M4/ladybirds.h", outdir.."Files_M4/src/ladybirds.h");
filecopy(resdir.."standard_M4/m4master_blinky.c", outdir.."Files_M4/src/m4master_blinky.c");
filecopy(resdir.."standard_M4/sysinit.c", outdir.."Files_M4/src/sysinit.c");
filecopy(resdir.."standard_M4/SharedStruct.h", outdir.."Files_M4/src/SharedStruct.h");















