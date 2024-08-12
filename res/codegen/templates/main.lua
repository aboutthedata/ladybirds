-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

init();

local prog = Ladybirds.Parse({filename=args.lbfile});
assert(prog, nil);
local x = Ladybirds.Export(prog);

outdir="";
render("mapping.template.lua", x);
