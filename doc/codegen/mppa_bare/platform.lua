-- Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

local mppa = Ladybirds.CreatePlatform();
local k1 = mppa:addcoretype{name="k1"}

local clustermems, dmas = {}, {}

for cc = 0, 15 do
    local mem = mppa:addmem{name=string.format("cc%d:mem", cc), size=2<<20};
    local dma = mppa:adddma{name=string.format("cc%d:dma", cc)};
    clustermems[cc], dmas[cc] = mem, dma;
        
    for pe = 0, 15 do
        local core = mppa:addcore{name=string.format("cc%d:pe%d", cc, pe), type=k1};
        mppa:addlink{core=core, mem=mem, readcost=10, writecost=10};
    end
end

for io = 0, 3 do
    local mem = mppa:addmem{name=string.format("io%d:mem", io), size=2<<20};
    local dma = mppa:adddma{name=string.format("io%d:dma", io)};
    clustermems[16+io], dmas[16+io] = mem, dma;
end

local bigmem = mppa:addmem{name="extmem", size=1<<30};
for from = 0, 19 do
    for to = 0, 19 do
        if from ~= to then
            mppa:adddmalink{from=clustermems[from], to=clustermems[to], controller=dmas[from], fixcost=300, writecost=5};
        end
    end
    mppa:adddmalink{from=clustermems[from], to=bigmem, controller=dmas[from], fixcost=2000, writecost=30};
    mppa:adddmalink{to=clustermems[from], from=bigmem, controller=dmas[from], fixcost=2000, writecost=30};
end

return mppa
