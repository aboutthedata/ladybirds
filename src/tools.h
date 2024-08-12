// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef TOOLS_H
#define TOOLS_H

#include <algorithm>
#include <string>
#include <sstream>
#include <vector>

#include "range.h"

#ifndef NDEBUG
#define DEBUG_CODE(x) x
#else
#define DEBUG_CODE(x)
#endif

//! A data type, essentially "boolean + value option 'Undecided'"
enum class Tristate : char { True = true, False = false, Undecided = -1};

//! The resource directory path of the application
extern std::string gResourceDir;

template<typename T, typename constind>
using CopyConst = typename std::conditional<std::is_const<constind>::value, const T, T>::type;

//! like sprintf, but returns a std::string
template<typename ... Args>
std::string strprintf( const char * format, Args ... args )
{
    char buf[1024];
    int size = snprintf( buf, sizeof(buf), format, args ... ); // size without '\0'
    
    if(size < 0) return "[snprintf error]";
    if(size < (int) sizeof(buf)-1) return std::string(buf, size);
        
    //string too big for static buffer, need dynamic allocation
    std::vector<char> dynbuf(size+1);
    snprintf( dynbuf.data(), size+1, format, args ... );
    return std::string(dynbuf.data(), size); // We don't want the '\0' inside
}

template<typename t>
std::string DumpToString(const t & obj)
{
    std::stringstream strm;
    strm << obj;
    return strm.str();
}

template<typename container_t, typename sortby_t> void Sort(container_t &container, sortby_t sortby)
{
    std::sort(container.begin(), container.end(),
              [&sortby](const auto &a, const auto &b){ return sortby(a) < sortby(b); });
}

//!Returns true if \p teststring is a valid identifier (begins with a letter; only letters, digits, underscore allowed).
//!Note we do not allow an underscore as the first character in order to simplify code generation.
bool CheckIdentifier(const std::string & teststring);

//!Given an array index vector and an array dimension vector (both in order of C array index – 0 leftmost, then 1, ...),
//!this function converts the multi-dimensional index to a one-dimensional index.
int FlattenIndex(std::vector<int>::const_iterator indexBegin, std::vector<int>::const_iterator indexEnd, 
                 std::vector<int>::const_iterator dimensionsBegin);

//!Given an array index vector and an array dimension vector (both in order of C array index – 0 leftmost, then 1, ...),
//!this function converts the multi-dimensional index to a one-dimensional index.
int FlattenIndex(std::vector<Ladybirds::gen::Range>::const_iterator indexBegin,
                 std::vector<Ladybirds::gen::Range>::const_iterator indexEnd, 
                 std::vector<int>::const_iterator dimensionsBegin);

//! Counterpart to \c FlattenIndex
std::vector<int> UnflattenIndex(int flatindex, std::vector<int>::const_iterator dimensionsBegin,
                                std::vector<int>::const_iterator dimensionsEnd);

//!@{ Returns a string with a C array index for a vector or range of indices, e.g. "[1][2][3]" for {1, 2, 3}.
std::string IndexString(std::vector<int>::const_iterator begin, std::vector<int>::const_iterator end);
inline std::string IndexString(const std::vector<int> & index) {return IndexString(index.begin(), index.end()); }
std::string IndexString(std::vector<Ladybirds::gen::Range>::const_iterator begin, 
                        std::vector<Ladybirds::gen::Range>::const_iterator end);
inline std::string IndexString(const Ladybirds::gen::Space & index)
    {return IndexString(index.begin(), index.end()); }
//!@}

//!@{ Returns the product of all the elements in the vector or the range
int Product(std::vector<int>::const_iterator begin, std::vector<int>::const_iterator end);
inline int Product(const std::vector<int> & vec) { return Product(vec.begin(), vec.end()); }
//!@}

//! Sums up all elements in \p cont according to the callback cb
template<typename container_t, typename callback_t>
auto Sum(container_t &cont, callback_t cb)
{
    auto it = std::begin(cont), itend = std::end(cont);
    if(it == itend) return 0;
    
    auto ret = cb(*it);
    while(++it != itend) ret += cb(*it);
    return ret;
}


//! Clones a vector of unique_ptrs by cloning the objects pointed to
template<typename T> std::vector<std::unique_ptr<T>> Clone(const std::vector<std::unique_ptr<T>> & vec)
{
    std::vector<std::unique_ptr<T>> ret;
    ret.reserve(vec.size());
    for(auto & up : vec) ret.push_back(std::make_unique<T>(*up));
    return ret;
}

template<typename t> struct VecDumpClass { const std::vector<t> & Vec; };
//! Helper for easily printing a vector of printable objects (e.g. with std::cout). Usage: std::cout << VecDump(myvec);
template<typename t> auto VecDump(const std::vector<t> & vec) {return VecDumpClass<t>({vec});}
template<typename t> std::ostream & operator<<(std::ostream & strm, const VecDumpClass<t> dump)
{
    strm << "( ";
    const char * sep = "";
    for(const t & elem : dump.Vec)
    {
        strm << sep << elem;
        sep = ", ";
    }
    return strm << " )";
}

template<typename key_t, typename map_t>
typename map_t::mapped_type FindOrDefault(map_t &map, key_t key, typename map_t::mapped_type defaultval)
{
    auto it = map.find(key);
    if(it == map.end()) return defaultval;
    else return it->second;
}

template<typename coll_t, typename fun_t, typename op_t> auto ComplexReduce(coll_t coll, fun_t fun, op_t op)
{
    auto it = std::begin(coll);
    auto itend = std::end(coll);
    assert(it != itend);
    auto ret = fun(*it);
    while(++it != itend) ret = op(ret, fun(*it));
    return ret;
}

template<typename coll_t, typename fun_t> auto Max(coll_t &coll, fun_t fun)
    { using t = const decltype(fun(*std::begin(coll)))&; return ComplexReduce(coll, fun, (t(*)(t,t)) &std::max); }
template<typename coll_t, typename fun_t> auto Max(coll_t &&coll, fun_t fun)
    { using t = const decltype(fun(*std::begin(coll)))&; return ComplexReduce(coll, fun, (t(*)(t,t)) &std::max); }
template<typename coll_t, typename fun_t> auto Min(coll_t &coll, fun_t fun)
    { using t = const decltype(fun(*std::begin(coll)))&; return ComplexReduce(coll, fun, (t(*)(t,t)) &std::min); }
template<typename coll_t, typename fun_t> auto Min(coll_t &&coll, fun_t fun)
    { using t = const decltype(fun(*std::begin(coll)))&; return ComplexReduce(coll, fun, (t(*)(t,t)) &std::min); 
}
#endif // TOOLS_H
