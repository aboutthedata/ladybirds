-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

lfs = require('lfs')
fastache = require('lua_fastache');
tools = Ladybirds.Tools()

args = Ladybirds.CmdLineArgs()

init = function()
    resdir=tools.realpath(debug.getinfo(2, "S").source:match("@(.*/)"))..'/';
    indir=tools.realpath(args.lbfile:match(".*/") or "")..'/';
    appname=args.lbfile:match("([^/%.]*)%..*$");
end

migratefromglobal = function(t)
    local hints = {}
    setmetatable(hints, {__gc = function(t)
        local T = {};
        for key,_ in pairs(t) do T[#T+1] = key; end
        table.sort(T);
        for _,out in ipairs(T) do print(out); end
    end});

    setmetatable(_G, {__index=function(table, key)
        local info = debug.getinfo(2, "lS")
        hints[string.format("%s:%d: %s", info.source:sub(2), info.currentline, key)] = true;
        return t[key];
    end })
end

printf = function(s,...)
    io.write(s:format(...));
    io.flush();
end

if args.verbose then
    vprintf = printf
else
    vprintf = function(s,...) end
end

genkernel = function(file, source)
    file = file:gsub('%.kernel$', '.c')
    io.open(outdir..file, "w+"):write(source)
end

-- Copy fromfile to tofile. If only one argument is given, copy indir..file to outdir..file.
-- Currently, instead of physically copying the file, this function creates a symlink with a relative path.
copy = function(fromfile, tofile)
    if(tofile == nil) then
        return copy(indir..fromfile, outdir..fromfile);
    end

    os.remove(tofile);
    tools.symlink(fromfile, tofile);
end

filecopy = function(from, to)
    io.open(to, "w"):write(io.open(from):read("*a"))
end

filecontents = function(path)
    local fid, err = io.open(path);
    if not fid then error(err, 2); end;
    local ret = fid:read("*a");
    io.close(fid);
    return ret;
end

render = function(file, view_model, template)
    printf("writing %s\n", file);
    if template then
        error("deprecated parameter template");
    end
    local template = fastache.parse(resdir..file..".mustache");
    template:render(outdir..file, view_model);
end

map2array = function(tbl)
    local ret = {}
    for key,val in pairs(tbl) do
        ret[#ret+1] = {key=key, value=val};
    end
    return ret
end

marklast = function(arr, prop)
    for _, obj in pairs(arr) do
        local arr = obj[prop];
        if #arr > 0 then arr[#arr].last = true; end;
    end;
    return arr;
end;

indexstring = function(index)
    local ret, sep = "(", "";
    for _,rg in ipairs(index) do
        ret = ret..sep..(rg.first < rg.last and rg.first..".."..rg.last or rg.first);
        sep = ", ";
    end
    return ret..")";
end;

printreport = function(x)
    printf("writing report\n");
    lfs.mkdir(outdir.."report");

    local thisdir = tools.realpath(debug.getinfo(1, "S").source:match("@(.*/)"));
    local template = fastache.parse(thisdir.."/report.html.mustache");
    template:render(outdir.."report/summary.html", {appname = appname, groups = x.groups});
    
    template = fastache.parse(thisdir.."/buffers.lua.mustache");
    template:render(outdir.."report/buffers.lua", x);
end;
