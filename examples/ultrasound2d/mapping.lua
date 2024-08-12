grouping = {};

for i = 0,62 do
   local base = i*63
   for n = 0,62 do
      grouping[string.format("GetIndex[%d]", base+n)] = i;
      grouping[string.format("Apodize[%d]", base+n)] = i;
   end
   grouping[string.format("Summation[%d]", i)] = i;
end
