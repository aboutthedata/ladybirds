grouping = {
    ["GenImage[0]"] = "cc0:pe15",
    ["OutputImage[0]"] = "cc0:pe15"
}

for i=0,15 do
    grouping["Canny[0].SobelX["..i.."]"] = "cc0:pe"..tostring(i);
    grouping["Canny[0].SobelY["..i.."]"] = "cc0:pe"..tostring(i);
    grouping["Canny[0].EdgeGradient["..i.."]"] = "cc0:pe"..tostring(i);
    grouping["Canny[0].NonMaxSuppression["..i.."]"] = "cc0:pe"..tostring(i);
end

