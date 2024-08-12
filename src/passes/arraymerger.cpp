// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "lua/pass.h"

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <list>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

#include "lua/luaenv.h"
#include "lua/luainterface.h"

#ifdef __GLIBCXX__

//dirty hack for allowing hashes of 64bit strings
namespace std {
  using s64string = basic_string<int64_t>;
  template<>
    struct hash<s64string>
    : public __hash_base<size_t, s64string>
    {
      size_t
      operator()(const s64string& __s) const noexcept
      { return std::_Hash_impl::hash(__s.data(),
                                     __s.length() * sizeof(int64_t)); }
    };

  template<>
    struct __is_fast_hash<hash<s64string>> : std::false_type
    { };
}
#endif //def __GLIBCXX__


namespace{

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ArrayMerger class

class ArrayMerger
{
public:
    using ItemRep = int64_t;
    static_assert(std::is_signed<lua_Integer>() && sizeof(lua_Integer) == sizeof(ItemRep), "Type mismatch");
    using ArrayRep = std::basic_string<ItemRep>; //using string here because a hash function is provided
    enum Signedness { Undef = 0, Unsigned = 1, Signed = 2};
    enum Errors { ErrorInvalidHandle = -1, ErrorWrongSign = -2, ErrorTooLarge = -3 };
    
    struct Data
    {
        int Bits;
        bool Signed;
        std::vector<ArrayRep> Arrays;
    };
    
private:
    struct IntType
    {
        int BitsMin = 0;
        int Bits = 0;
        Signedness Sign = Undef;
        std::unordered_map<ArrayRep, int> Arrays;
        
        int Index = -1;
    };
    
    std::list<IntType> Types_; //using list so we can sort it later on without invalidating pointers to elements
    std::vector<IntType*> Handles_;
    
public:
    ArrayMerger() = default;
    
    int AddType(int bits = 0, Signedness sign = Undef)
    {
        if(bits > 0 && sign != Undef) //see if this type already exists
        {
            auto it = std::find_if(Types_.begin(), Types_.end(),
                                [bits, sign](auto &type) { return type.Bits == bits && type.Sign == sign; });
            if(it != Types_.end()) return it->Index;
        }
        
        //Otherwise, create new type
        auto it = Types_.emplace(Types_.end());
        it->Bits = bits;
        it->Sign = sign;
        it->Index = Handles_.size();
        Handles_.push_back(&*it);
        return it->Index;
    }
    
    int AddArray(ArrayRep array, int type)
    {
        if(size_t(type) >= Handles_.size()) return ErrorInvalidHandle;
        IntType &t = *Handles_[type];
        
        if(!array.empty())
        {//get required bit width and signedness
            auto range = std::minmax_element(array.begin(), array.end());
            int sparebits = __builtin_clzl(*range.second);
            int needsign = 0;
            if(*range.first < 0)
            {
                needsign = 1;
                sparebits = std::min(sparebits,  __builtin_clzl(~*range.first));
                //The minimum number of bits we get here may be one too low, but we know we need one more
                                //bit for the sign, so this is all right.
            }
            int minbits = sizeof(ItemRep)*8 - sparebits + needsign;
            static_assert(std::is_same<long, ItemRep>(), "Type mismatch for __builtin_ctzl");
            
            if(needsign)
            {
                switch(t.Sign)
                {
                    case Signed:   break;
                    case Unsigned: return ErrorWrongSign;
                    case Undef:
                        t.Sign = Signed;
                        t.BitsMin++;
                }
            }
            else
            {
                switch(t.Sign)
                {
                    case Unsigned: break;
                    case Signed:   ++minbits; break;
                    case Undef:
                        if(t.Bits == minbits) t.Sign = Unsigned;
                }
            }
            
            t.BitsMin = std::max(t.BitsMin, minbits);
            if(t.Bits != 0 && t.BitsMin > t.Bits) return ErrorTooLarge;
        }
        
        auto res = t.Arrays.emplace(std::move(array), t.Arrays.size());
        return res.first->second;
    }
    
    void Finalize(std::vector<int> &finaltypes, std::vector<std::vector<int>> &indices, std::vector<Data> &data)
    {
        if(Types_.empty()) return;
        
        // determine the necessary element size for each array
        for(auto &t: Types_)
        {
            if(t.Bits != 0) continue;
            
            int bits = 8;
            while(bits < t.BitsMin) bits *= 2;
            assert(bits <= 64);
            t.Bits = bits;
            if(bits == t.BitsMin && t.Sign == Undef) t.Sign = Unsigned;
        }
        
        indices.resize(Handles_.size());
        
        // Now merge different types. TODO: Find cleverer method for choosing which types to merge.
        Types_.sort([](auto &t1, auto &t2) { return t1.Bits < t2.Bits || (t1.Bits == t2.Bits && t1.Sign > t2.Sign); });
        for(auto it = Types_.begin(), itnext = std::next(it), itend = Types_.end(); itnext != itend; )
        {
            auto &t = *it, &t1 = *itnext;
            if((t1.Bits != t.Bits) ||
               (t1.Sign != t.Sign && t1.Sign != Undef))
            {
                it = itnext++;
                continue;
            }
            
            // merge t1 into t
            std::vector<int> t1indices(t1.Arrays.size());
            for(auto &entry : t1.Arrays)
            {
                auto res = t.Arrays.emplace(std::move(entry.first), t.Arrays.size());
                t1indices[entry.second] = res.first->second;
            }
            indices[t1.Index] = std::move(t1indices);
            Handles_[t1.Index] = &t;
            itnext = Types_.erase(itnext);
        }
        
        data.clear(); data.reserve(Types_.size());
        for(auto &t : Types_)
        {
            std::vector<int> tindices(t.Arrays.size());
            std::iota(tindices.begin(), tindices.end(), 0);
            indices[t.Index] = std::move(tindices);

            t.Index = data.size();
            data.emplace_back();
            auto &sys = data.back();
            sys.Bits = t.Bits;
            sys.Signed = (t.Sign != Unsigned);
            sys.Arrays.resize(t.Arrays.size());
            for(auto &entry : t.Arrays) sys.Arrays[entry.second] = std::move(entry.first);
        }
        
        finaltypes.clear(); finaltypes.reserve(Handles_.size());
        for(auto *ptype : Handles_) finaltypes.push_back(ptype->Index);
        
        Handles_.clear();
        Types_.clear();
    }
    
    const char *GetErrorDescription(int err)
    {
        if(err >= 0) return "Success";
        switch(err)
        {
            case ErrorInvalidHandle: return "Invalid handle for integer category";
            case ErrorWrongSign: return "Trying to insert negative values into unsigned array";
            case ErrorTooLarge: return "Provided values are too large for specified bit width";
            default: return "Unknown error";
        }
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lua Wrapper

class ArrayMergerLua : public ArrayMerger
{
public:
    ArrayMergerLua() = default;
    
    static constexpr const char* GetLuaName() { return "Ladybirds::ArrayMerger"; }
    
    int AddType(lua_State *lua)
    {
        auto bits = luaL_optinteger(lua, 2, 0);
        auto valid = {0, 8, 16, 32, 64};
        if(std::find(valid.begin(), valid.end(), bits) == valid.end()) 
            return luaL_argerror(lua, 2, "Invalid integer width");
        
        Signedness sign = Undef;
        if(!lua_isnoneornil(lua, 3))
            sign = lua_toboolean(lua, 3) ? Signed : Unsigned;
        
        int res = ArrayMerger::AddType(bits, sign);
        if(res < 0) return luaL_error(lua, GetErrorDescription(res));
        
        lua_pushinteger(lua, res+1);
        return 1;
    }
    
    int AddArray(lua_State *lua)
    {
        luaL_checktype(lua, 2, LUA_TTABLE);
        int type = luaL_checkinteger(lua, 3)-1;
        
        int arraylen = luaL_len(lua, 2);
        ArrayRep array(arraylen, 0);
        for(int i = 0; i < arraylen; ++i)
        {
            lua_rawgeti(lua, 2, i+1);
            if(!lua_isinteger(lua, -1))
                return luaL_error(lua, "Invalid array element #%d: Not an integer", i+1);
            array[i] = lua_tointeger(lua, -1);
            lua_pop(lua, 1);
        }
        
        int res = ArrayMerger::AddArray(array, type);
        if(res < 0) return luaL_error(lua, GetErrorDescription(res));
        
        lua_pushinteger(lua, res+1);
        return 1;
    }
    
    int Finalize(lua_State *lua)
    {
        std::vector<int> finaltypes;
        std::vector<std::vector<int>> indices;
        std::vector<Data> data;
        
        ArrayMerger::Finalize(finaltypes, indices, data);
        
        auto exportarray = [lua](auto &arr, auto fn)
        {
            lua_createtable(lua, arr.size(), 0);
            int i = 0;
            for(auto &elem : arr)
            {
                fn(elem);
                lua_rawseti(lua, -2, ++i);
            }
        };
        auto exportidxarray = [lua, exportarray](auto &arr)
        {
            exportarray(arr, [lua](lua_Integer i){lua_pushinteger(lua, i+1);});
        };
        exportidxarray(finaltypes);
        exportarray(indices, exportidxarray);
        exportarray(data, [lua,exportarray](auto &t)
        {
            lua_createtable(lua, 0, 3);
            lua_pushinteger(lua, t.Bits);   lua_setfield(lua, -2, "bits");
            lua_pushboolean(lua, t.Signed); lua_setfield(lua, -2, "signed");
            exportarray(t.Arrays, [lua, exportarray](auto &arr){exportarray(arr, [lua](lua_Integer i){lua_pushinteger(lua, i);});});
            lua_setfield(lua, -2, "arrays");
        });
        return 3;
    }
};


/// CreateArrayMergerPass: Pseudo pass returning new ArrayMerger object
class CreateArrayMergerPass : public Ladybirds::lua::Pass
{
    using Pass::Pass;
    virtual int Run(lua_State *lua) override
    {
        Ladybirds::lua::LuaInterface<ArrayMergerLua> iface(lua, 
        {
            {"newtype", &ArrayMergerLua::AddType},
            {"addarray", &ArrayMergerLua::AddArray},
            {"finalize", &ArrayMergerLua::Finalize}
        });
        iface.CreateObject();
        return 1;
    }
};
static CreateArrayMergerPass MyPass("CreateArrayMerger", nullptr);

} //namespace ::
