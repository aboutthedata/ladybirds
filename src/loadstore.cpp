// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "loadstore.h"

#include <sstream>


namespace Ladybirds { namespace loadstore {

using std::string;

template<typename T>
static inline bool IoHelper(LoadStore & ls, const char* name, T& var, bool required, const T & defaultval)
{
    if(name && !ls.PrepareNamedVar(name, required))
    {
        if(required) return false;
        else
        {
            if(ls.IsLoading()) var = defaultval;
            return true;
        }
    }
    
    bool ret = ls.RawIO(var);
    if(!ret)
    {
        ls.Error("while processing element %s", name);
        if(ls.IsLoading()) var = defaultval;
    }
    
    return ls.FlushNamedVar(name, true) && ret;
}

template<typename T>
static inline bool IoHelper(LoadStore & ls, const char* name, T& var, bool required, T defaultval, T minval, T maxval)
{
    if(!IoHelper(ls, name, var, required, defaultval)) return false;
    
    if(ls.IsStoring()) return true;
    
    if(var < minval || var > maxval)
    {
        std::stringstream strm;
        strm << name << " must be between " << minval << " and " << maxval << ".";
        ls.Error(strm.str().c_str());
        var = defaultval;
        return false;
    }

    return true;
}

template<typename T>
static inline bool IoHelper(LoadStore & ls, const char* name, std::vector<T>& vec, bool required, T minval, T maxval)
{
    if(name && !ls.PrepareNamedVar(name, required)) return !required;
    
    bool ret;
    if(ls.IsLoading())
    {
        assert(vec.empty());
        
        auto loadcallback = [&vec, name, minval, maxval](LoadStore & ls)
        {
            T var;
            if(!ls.RawIO(var)) return false;
            
            if(var < minval || var > maxval)
            {
                std::stringstream strm;
                strm << "Each element of " << name << " must be between " << minval << " and " << maxval << ".";
                ls.Error(strm.str().c_str());
                return false;
            }
            
            vec.push_back(std::move(var));
            return true;
        };
        ret = ls.RawArrayIO(0, loadcallback);
    }
    else
    {
        auto iter = vec.begin();
        auto storecallback = [&iter](LoadStore & ls)
        {
            return ls.RawIO(*iter++);
        };
        
        ret = ls.RawArrayIO(vec.size(), storecallback);
    }

    return ls.FlushNamedVar(name, true) & ret;
}

template<typename T>
static inline bool IoHelper(LoadStore & ls, const char* name, LoadStore::Table<T>& table, bool required,
                            T minval, T maxval)
{
    if(name && !ls.PrepareNamedVar(name, required)) return !required;
    
    bool ret;
    if(ls.IsLoading())
    {
        assert(table.empty());
        
        auto loadcallback = [&table, name, minval, maxval](string &key, LoadStore &ls)
        {
            T var;
            if(!ls.RawIO(var)) return false;
            
            if(var < minval || var > maxval)
            {
                std::stringstream strm;
                strm << "Each element of " << name << " must be between " << minval << " and " << maxval << ".";
                ls.Error(strm.str().c_str());
                return false;
            }
            
            table.emplace(std::move(key), std::move(var));
            return true;
        };
        ret = ls.RawMapIO(0, loadcallback);
    }
    else
    { //storing
        auto iter = table.begin();
        auto storecallback = [&iter](string &key, LoadStore &ls)
        {
            key = iter->first;
            return ls.RawIO((iter++)->second);
        };
        
        ret = ls.RawMapIO(table.size(), storecallback);
    }

    return ls.FlushNamedVar(name, true) & ret;
}

bool LoadStore::IO(const char* name, bool& var, bool required, bool defaultval)
    { return loadstore::IoHelper(*this, name, var, required, defaultval); }
bool LoadStore::IO(const char* name, int& var, bool required, int defaultval, int minval, int maxval)
    { return loadstore::IoHelper(*this, name, var, required, defaultval, minval, maxval); }
bool LoadStore::IO(const char* name, double& var, bool required, double defaultval, double minval, double maxval)
    { return loadstore::IoHelper(*this, name, var, required, defaultval, minval, maxval); }
bool LoadStore::IO(const char* name, LoadStorableCompound& var, bool required, const LoadStorableCompound& defaultval)
    { return loadstore::IoHelper(*this, name, var, required, defaultval); }
bool LoadStore::IO(const char* name, std::string& var, bool required, const std::string& defaultval)
    { return loadstore::IoHelper(*this, name, var, required, defaultval); }

bool LoadStore::IO_Register(const char * name, Referenceable & var, bool required, const Referenceable & defaultval)
{
    if(name && !PrepareNamedVar(name, required)) return !required;
    
    bool ret = RawIO_Register(var);
    if(!ret)
    {
        Error("while processing element %s", name);
        if(IsLoading()) var = defaultval;
    }
    
    return FlushNamedVar(name, true) && ret;
}
    
bool LoadStore::IO(const char* name, std::vector<bool>& vec, bool required)
    { return IoHelper<bool, std::vector<bool>::reference, &LoadStore::VecBoolLoad, &LoadStore::VecBoolStore>
                     (name, vec, required); }
bool LoadStore::IO(const char* name, std::vector< double >& vec, bool required, double minval, double maxval)
    { return loadstore::IoHelper(*this, name, vec, required, minval, maxval); }
bool LoadStore::IO(const char* name, std::vector< int >& vec, bool required, int minval, int maxval)
    { return loadstore::IoHelper(*this, name, vec, required, minval, maxval); }
bool LoadStore::IO(const char* name, std::vector<std::string>& vec, bool required)
    { return IoHelper<std::string, std::string&, &LoadStore::RawIO>(name, vec, required); }

bool LoadStore::IO(const char* name, Table<bool>& table, bool required)
    { return IoHelper<bool, bool&, &LoadStore::RawIO>(name, table, required); }
bool LoadStore::IO(const char* name, Table<double >& table, bool required, double minval, double maxval)
    { return loadstore::IoHelper(*this, name, table, required, minval, maxval); }
bool LoadStore::IO(const char* name, Table<int >& table, bool required, int minval, int maxval)
    { return loadstore::IoHelper(*this, name, table, required, minval, maxval); }
bool LoadStore::IO(const char* name, Table<std::string>& table, bool required)
    { return IoHelper<std::string, std::string&, &LoadStore::RawIO>(name, table, required); }

bool LoadStore::IORef(const char* name, Referenceable*& ref, const char* type,
                      bool required, Referenceable* defaultval)
{
    if(name && !PrepareNamedVar(name, required)) return !required;
    
    bool ret = RawIORef(ref, type, required);
    if(!ret)
    {
        Error("while processing element %s", name);
        if(IsLoading()) ref = defaultval;
    }
    
    return FlushNamedVar(name, true) && ret;
}

bool LoadStore::IOHandle(const char* name, Referenceable*& ref, const char* type, void *context,
                      bool required, Referenceable* defaultval)
{
    if(name && !PrepareNamedVar(name, required)) return !required;
    
    bool ret = RawIOHandle(ref, context, type, required);
    if(!ret)
    {
        Error("while processing element %s", name);
        if(IsLoading()) ref = defaultval;
    }
    
    return FlushNamedVar(name, true) && ret;
}

bool LoadStore::IO(const char* name, ValueStringInterface& var, bool required, const std::string& defaultval)
{
    string s;
    if(IsStoring()) s = var.Get();
    
    if(!IO(name, s, required, defaultval)) return false;
    
    string err;
    if(IsLoading() && !var.Set(s, &err))
    {
        Error("%s: %s", name, err.c_str());
        return false;
    }
    return true;
}

}} //namespace Ladybirds::loadstore
