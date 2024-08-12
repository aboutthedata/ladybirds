// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GEN_MULTICRITCMP_H
#define LADYBIRDS_GEN_MULTICRITCMP_H

#include <algorithm>
#include <functional>
#include <tuple>
#include <type_traits>

namespace Ladybirds { namespace gen {

/** Multi-criteria comparison, compatible with standard library sort and other algorithms.**/
template<typename... Args>
struct MultiCriteriaComparison
{
private:
    std::tuple<Args...> CmpFunctions;
    
public:
    constexpr MultiCriteriaComparison(Args... args) : CmpFunctions(std::forward<Args>(args)...) {}
    
    template<typename T1, typename T2>
    constexpr bool operator() (T1& o1, T2& o2)
    {
        return (DoCompare<T1, T2, 0>(o1, o2) < 0);
    }
    
private:   
    template<typename T1, typename T2, int i>
    auto DoCompare(T1& o1, typename std::enable_if<(i < sizeof...(Args)), T2&>::type &o2)
    {
        auto curcmp = std::get<i>(CmpFunctions)(o1, o2);
        return ((i == sizeof...(Args)-1) || curcmp != 0) ? curcmp : DoCompare<T1, T2, i+1>(o1, o2);
    }
    template<typename T1, typename T2, int i>
    static int DoCompare(T1&, typename std::enable_if<(i == sizeof...(Args)), T2>::type&)
    {
        return 0;
    }
};


/** Multi-criteria comparison, compatible with standard library sort and other algorithms.
  * The arguments must be any number of comparison functions of the form int cmp(a, b).
  * For each such cmp, it must hold that cmp < 0 if a < b, cmp == 0 if a == b, cmp > 0 if a > b.
 **/

template<typename... Args>
constexpr auto MultiCritCmp(Args... args)
{
    return MultiCriteriaComparison<Args...>(std::forward<Args>(args)...);
}


/** Finds the smallest element in [\p begin, \p end) for which \p filter returns true. 
  * Comparison is done by using \p cmp. 
  * \p cmp is a std compatible comparison function (\p cmp(a, b) must return true if a < b).
 **/
template<typename It1_T, typename It2_T, typename Filter, typename Compare>
It1_T MinElementIf(It1_T begin, It2_T end, Filter filter, Compare cmp)
{
    while(begin != end && !filter(*begin)) ++begin;
    if(begin == end) return begin;

    auto smallest = begin;
    while(++begin != end)
    {
        if(filter(*begin) && cmp(*begin, *smallest)) smallest = begin;
    }
    return smallest;
}

/** Finds the greatest element in [\p begin, \p end) for which \p filter returns true. 
  * Comparison is done by using \p cmp. 
  * \p cmp is a std compatible comparison function (\p cmp(a, b) must return true if a < b).
 **/
template<typename It_T, typename Filter, typename Compare>
It_T MaxElementIf(It_T begin, It_T end, Filter filter, Compare cmp)
{
    begin = std::find_if(begin, end, filter);
    if(begin == end) return end;

    auto greatest = begin;
    while(++begin != end)
    {
        if(filter(*begin) && cmp(*greatest, *begin)) greatest = begin;
    }
    return greatest;
}

} //namespace gen
} //namespace Ladybirds
#endif // LADYBIRDS_GEN_MULTICRITCMP_H

