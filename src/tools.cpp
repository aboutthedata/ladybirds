// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "tools.h"

#include <cctype>
#include <string>
#include <algorithm>
#include <sstream>
#include <numeric>

using namespace std;
using namespace Ladybirds::gen;

bool CheckIdentifier(const std::string& teststring)
{
    return isalpha(teststring[0]) 
        && std::all_of(teststring.begin(), teststring.end(), [](char c) {return isalnum(c) || c == '_';});
}

int FlattenIndex(std::vector<int>::const_iterator indexBegin, std::vector<int>::const_iterator indexEnd, 
                 std::vector<int>::const_iterator dimensionsBegin)
{
    int ret = 0;
    while(indexBegin != indexEnd)
    {
        ret *= *dimensionsBegin++;
        ret += *indexBegin++;
    }
    return ret;
}

int FlattenIndex(std::vector<Range>::const_iterator indexBegin, std::vector<Range>::const_iterator indexEnd, 
                 std::vector<int>::const_iterator dimensionsBegin)
{
    int ret = 0;
    while(indexBegin != indexEnd)
    {
        ret *= *dimensionsBegin++;
        ret += (indexBegin++)->first();
    }
    return ret;
}

std::vector<int> UnflattenIndex(int flatindex, vector<int>::const_iterator dimensionsBegin, 
                                std::vector<int>::const_iterator dimensionsEnd)
{
    int size = dimensionsEnd - dimensionsBegin;
    vector<int> ret(size);
    for(auto it = ret.rbegin(), itend = ret.rend(); it != itend; ++it)
    {
        int curdim = *--dimensionsEnd;
        *it = flatindex % curdim;
        flatindex /= curdim;
    }
    assert(flatindex == 0);
    
    return ret;
}


std::string IndexString(std::vector<int>::const_iterator begin, std::vector<int>::const_iterator end)
{
    std::stringstream strm;
    for(; begin != end; ++begin) strm << '[' << *begin << ']';
    return strm.str();
}

std::string IndexString(std::vector<Ladybirds::gen::Range>::const_iterator begin, 
                        std::vector<Ladybirds::gen::Range>::const_iterator end)
{
    std::stringstream strm;
    for(; begin != end; ++begin) strm << '[' << *begin << ']';
    return strm.str();
}

int Product(std::vector<int>::const_iterator begin, std::vector<int>::const_iterator end)
{
    return std::accumulate(begin, end, 1, std::multiplies<int>());
}
