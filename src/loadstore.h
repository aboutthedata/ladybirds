// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LOADSTORE_H
#define LOADSTORE_H

#include <stdio.h>
#include <climits>
#include <cfloat>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "graph/presdeque.h"

namespace Ladybirds { namespace loadstore {

class ValueStringInterface;


//! Interface for an object consisting of multiple variables (compound) that can be loaded/stored.
//! Override LoadStoreMembers to implement this functionality.
class LoadStorableCompound
{
public:
    //! Loads/stores all member variables using \p ls.
    //! Files an \c Error() and returns false on failure.
    virtual bool LoadStoreMembers(class LoadStore & ls) = 0;
    
    //! Loads all members from a single variable description ("short cut")
    virtual bool LoadFromShortcut(class LoadStore & ls) { return false; }
    
    virtual ~LoadStorableCompound() {}
};




class Referenceable : public LoadStorableCompound
{
public:
    virtual const char * GetTypeString() = 0;
};

#define ADD_CLASS_SIGNATURE(class) \
public:\
    static constexpr const char * const TypeString = #class;\
    virtual const char * GetTypeString() override { return TypeString;}



//! Base class for loading and storing objects from/to different kinds of sources/targets.
/** Derived classes provide the implementation.
    Provides an interface for loading/storing simple types, arrays and compounds (objects derived of \c LoadStorableCompound).
    Also provides an error reporting mechanism. **/
class LoadStore
{
public:
    template<typename T> using Table = std::unordered_map<std::string, T>;
    
    class TemporaryContext
    {
    private:
        LoadStore &LS;
        void *pOldContext;
        
    public:
        TemporaryContext(LoadStore &ls, void *newcontext) : LS(ls), pOldContext(ls.UserContext) 
            { ls.UserContext = newcontext; }
        ~TemporaryContext() { LS.UserContext = pOldContext; }
    };
    
public:
    void *UserContext; ///< Free for the user to store any context useful during storing or loading.
    
protected:
    enum OperationType{Store, Load};
    const OperationType OpType_;
    
    inline LoadStore(OperationType opType) : OpType_(opType) {}
    
public:
    virtual ~LoadStore() {}
    
    //! Returns true if this object is there for loading data
    inline bool IsLoading() const { return OpType_ == Load; }
    //! Returns true if this object is there for storing data
    inline bool IsStoring() const { return OpType_ == Store; }
    
    //! Use this function to report an error.
    /** It may either end the loading/storing process by throwing an exception or just log the error,
     *  allowing continuation of the process and identification of possible further errors.
     *  Accepts printf-like arguments. **/
    virtual void Error(const char * msg, ...) = 0;
    
    
    //! Prepares reading/writing a named variable.
    /** The next call to RawIO(x) will then access this variable.
     *  Files an Error() (if \p showErrorMsg is \c true) and returns \c false if the operation was unsuccessful.**/
    virtual bool PrepareNamedVar(const char * name, bool showErrorMsg = true) = 0;

    //! Finishes reading/writing a named variable.
    /** (After a successful call to RawIO(x).)
     *  Files an Error() (if \p showErrorMsg is \c true) and returns \c false if the operation was unsuccessful.**/
    virtual bool FlushNamedVar(const char * name, bool showErrorMsg = true){ return true;};
    
    //!@{
    //! Reads/writes a variable referenced by \p var.
    /** The variable should be ready for reading, e.g. by \c PrepareNamedVar having been called before.
     *  Files an \c Error() and returns \c false on failure.
     *  \p var is only modified upon successful read.**/
    virtual bool RawIO(bool & var) = 0;
    virtual bool RawIO(int & var) = 0;
    virtual bool RawIO(double & var) = 0;
    virtual bool RawIO(std::string & var) = 0;
    virtual bool RawIO(LoadStorableCompound & var) = 0;
    //!@}
    
    //! Reads/writes a reference given by \p ref to an object previously registered by IO_Register.
    /** The variable should be ready for reading, e.g. by \c PrepareNamedVar having been called before.
     *  Files an \c Error() and returns \c false on failure.
     *  \p ref only modified upon successful read.**/
    virtual bool RawIORef(Referenceable *& ref, const char * type, bool required) = 0;

    //! Reads/writes a handle to an object given by \p ref.
    /** Handles are pointers to internal objects which, however, cannot be accessed from outside.
     *  As a result, the only useful operation with handles is to load handles that have been stored before.
     *  \p context is included in the handle when storing and checked for equality when loading.
     *  Passing a null pointer to context means the context is not checked.
     *  The programmer needs to ensure that the lifespan of the object is the same as that of the handle or at least 
     *  of the context. Passing a pointer to the LoadStore object for \p context may instruct the LoadStore object to
     *  manage the lifetime of the referenced object automatically.
     * 
     *  The variable should be ready for reading, e.g. by \c PrepareNamedVar having been called before.
     *  Files an \c Error() and returns \c false on failure.
     *  \p ref only modified upon successful read.**/
    virtual bool RawIOHandle(Referenceable *& ref, const void *context, const char * type, bool required) = 0;

    
    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
    inline bool RawIORef(T *& ref)
    {
        Referenceable* pref = ref;
        bool ret = RawIORef(pref, T::TypeString, true);
        ref = static_cast<T*>(pref);
        return ret;
    }

    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
    inline bool RawIOHandle(T *&ref, void *context)
    {
        Referenceable* pref = ref;
        bool ret = RawIOHandle(pref, context, T::TypeString, true);
        ref = static_cast<T*>(pref);
        return ret;
    }
    
    //! Reads/writes an array of variables.
    /** When writing, \p nItems items are written.
     *  Each item is first prepared, then \p callback is called.
     *  The process is *not* interrupted when \p callback returns \c false, however.
     *  Files an \p Error() and returns \c false if no array can be read/written.
     *  Otherwise, returns \c false only if one of the callbacks returned false.*/
    virtual bool RawArrayIO(int nItems, std::function<bool(LoadStore &)> callback) = 0;
    //! Reads/writes an map of strings to variables.
    /** When writing, \p nItems items are written.
     *  Each item is first prepared, then \p callback is called.
     *  Its first parameter, on reading, is the key for the object.
     *  On writing, it must be set to that key by \p callback.
     *  The process is *not* interrupted when \p callback returns \c false, however.
     *  Files an \p Error() and returns \c false if no array can be read/written.
     *  Otherwise, returns \c false only if one of the callbacks returned false.*/
    virtual bool RawMapIO(int nItems, std::function<bool(std::string &, LoadStore &)> callback) = 0;
    
    //! \brief Reads/writes an object referenced by \p obj (using RawIO) and registers it in an internal database
    //!        such that it can later be referenced and calls to \c IORef will succeed.
    /** Files an \c Error() and returns \c false on failure.
     *  \p obj only modified upon successful read.**/
    virtual bool RawIO_Register(Referenceable & obj) = 0;
    
    
    
    //!@{
    //! Reads/writes a variable referenced by \p var and with the name \p name.
    /** When reading, if \p required is false and no variable with the name \p name is provided true is returned.
     *  Otherwise, if a read operation fails, \p var is always set to \p defaultval,
     *   an \c Error() is filed and \c false is returned.
     *  Some overloads have a \p minval and a \p maxval argument that specify the minimum/maximum allowed value.
     *  If the value being read is outside that range, an \c Error() is filed and \c false is returned.**/
    
    bool IO(const char * name, bool & var, bool required = true, bool defaultval=false);
    bool IO(const char * name, int & var, bool required = true, int defaultval=0,
            int minval=INT_MIN, int maxval=INT_MAX);
    bool IO(const char * name, double & var, bool required = true, double defaultval=0,
        double minval=DBL_MIN, double maxval=DBL_MAX);
    bool IO(const char * name, std::string & var, bool required = true, const std::string & defaultval = "");
    bool IO(const char * name, ValueStringInterface & var, bool required = true, const std::string & defaultval = "");
    //! \copydoc IO Overloaded version for `required = true`, without default value
    inline bool IO(const char * name, LoadStorableCompound & var) { return IO(name, var, true, var); }
    bool IO(const char * name, LoadStorableCompound & var, bool required, const LoadStorableCompound & defaultval);
    inline bool IO_Register(const char * name, Referenceable & var) { return IO_Register(name, var, true, var); }
    bool IO_Register(const char * name, Referenceable & var, bool required, const Referenceable & defaultval);
    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
    bool IORef(const char* name, T*& ref, bool required = true, T* defaultval = nullptr)
    {
        Referenceable* pref = ref;
        bool ret = IORef(name, pref, T::TypeString, required, defaultval);
        ref = static_cast<T*>(pref);
        return ret;
    }
    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
    bool IOHandle(const char* name, T*& ref, void *context, bool required = true, T* defaultval = nullptr)
    {
        Referenceable* pref = ref;
        bool ret = IOHandle(name, pref, T::TypeString, context, required, defaultval);
        ref = static_cast<T*>(pref);
        return ret;
    }
    //!@}
    
    //!@{
    //! Reads/writes a vector referenced by \p vec and with the name \p name.
    /** \p vec must be empty.
     *  When reading, if \p required is false and no variable with the name \p name is provided true is returned.
     *  Otherwise, if a read operation fails, an \c Error() is filed and \c false is returned.
     *  If the operation fails for individual items, all the other items will be contained in \p vec.
     *  If the entire array read operation fails, \p vec will be empty.
     *  Some overloads have a \p minval and a \p maxval argument that specify the minimum/maximum allowed value for
     *   each single element. If a value being read is outside that range, this element will not be contained in vec,
     *   an \c Error() is filed and \c false is returned. **/
    
    bool IO(const char * name, std::vector<bool> & vec, bool required = true);
    bool IO(const char * name, std::vector<int> & vec, bool required = true,
            int minval=INT_MIN, int maxval=INT_MAX);
    bool IO(const char * name, std::vector<double> & vec, bool required = true,
        double minval=DBL_MIN, double maxval=DBL_MAX);
    bool IO(const char * name, std::vector<std::string> & vec, bool required = true);
    /// \copydoc IO Can only be implemented as a template, for a `vector<Derived>` is not a `vector<Base>`.
    /// The 2nd template parameter can be ignored; it just instructs the compiler to use this template only if
    /// \p T is derived from LoadStorableCompound.
    template<typename T, typename std::enable_if<std::is_base_of<LoadStorableCompound, T>::value>::type* = nullptr>
        bool IO(const char * name, std::vector<T>& vec, bool required = true);
    template<typename T, typename std::enable_if<std::is_base_of<LoadStorableCompound, T>::value>::type* = nullptr>
        bool IO(const char * name, std::vector<std::unique_ptr<T>>& vec, bool required = true);
    template<typename T>
        bool IO(const char * name, std::vector<std::vector<T>> & vec, bool required = true);
    //!@}
        
    //!@{
    //! Reads/writes a map referenced by \p table and with the name \p name.
    /** \p table must be a string to something map, and empty.
     *  When reading, if \p required is false and no variable with the name \p name is provided true is returned.
     *  Otherwise, if a read operation fails, an \c Error() is filed and \c false is returned.
     *  If the operation fails for individual items, all the other items will be contained in \p vec.
     *  If the entire array read operation fails, \p table will be empty.
     *  Some overloads have a \p minval and a \p maxval argument that specify the minimum/maximum allowed value for
     *   each single element. If a value being read is outside that range, this element will not be contained in \p table,
     *   an \c Error() is filed and \c false is returned. **/
    
    bool IO(const char * name, Table<bool> & vec, bool required = true);
    bool IO(const char * name, Table<int> & vec, bool required = true,
            int minval=INT_MIN, int maxval=INT_MAX);
    bool IO(const char * name, Table<double> & vec, bool required = true,
        double minval=DBL_MIN, double maxval=DBL_MAX);
    bool IO(const char * name, Table<std::string> & vec, bool required = true);
    /// \copydoc IO Can only be implemented as a template,
    ///  for a `map<std::string, Derived>` is not a `map<std::string, Base>`.
    /// The 2nd template parameter can be ignored; it just instructs the compiler to use this template only if
    /// \p T is derived from LoadStorableCompound.
    template<typename T, typename std::enable_if<std::is_base_of<LoadStorableCompound, T>::value>::type* = nullptr>
        bool IO(const char * name, Table<T>& vec, bool required = true);
    template<typename T, typename std::enable_if<std::is_base_of<LoadStorableCompound, T>::value>::type* = nullptr>
        bool IO(const char * name, Table<std::unique_ptr<T>>& vec, bool required = true);
    template<typename T>
        bool IO(const char * name, Table<std::vector<T>> & vec, bool required = true);
    //!@}
        
    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
        bool IO_Register(const char * name, std::vector<T>& vec, bool required = true);
    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
        bool IO_Register(const char * name, std::vector<std::unique_ptr<T>>& vec, bool required = true);
    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
        bool IO_Register(const char * name, graph::ContainerRange<graph::PresDeque<T>> vec, bool required = true);
    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
        bool IORef(const char* name, std::vector<T*>& vec, bool required = true);
    template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type* = nullptr>
        bool IOHandles(const char* name, std::vector<T*> &vec, void *pcontext, bool required = true);
        
        
private:
    bool IORef(const char* name, Referenceable*& ref, const char* type,
               bool required = true, Referenceable* defaultval = nullptr);
    bool IOHandle(const char* name, Referenceable*& ref, const char* type, void *context,
               bool required = true, Referenceable* defaultval = nullptr);
    //! \internal Helper function template for reading/writing arrays.
    //! Only declared as a private member in order to make it inaccessible to outside
    template<typename T, typename param_t, 
             bool (LoadStore::*loadfun)(param_t), bool (LoadStore::*storefun)(param_t)=loadfun>
    bool IoHelper(const char * name, std::vector<T>& vec, bool required);
    //! \internal Helper function template for reading/writing tables.
    //! Only declared as a private member in order to make it inaccessible to outside
    template<typename T, typename param_t, 
             bool (LoadStore::*loadfun)(param_t), bool (LoadStore::*storefun)(param_t)=loadfun>
    bool IoHelper(const char * name, Table<T> &table, bool required);
    //! \internal Helper such that the array template for vector<bool> fits as well
    inline bool VecBoolLoad(std::vector<bool>::reference ref) { bool b; return RawIO(b) && (ref = b, true); }
    inline bool VecBoolStore(std::vector<bool>::reference ref) { bool b = ref; return RawIO(b); }
    inline bool RawIO(bool && var) { assert(IsStoring()); return RawIO(var); }
    //! \internal Helper such that the array template for vector<unique_ptr<LoadStorableCompound>> fits as well
    template<typename T> inline bool RawUPIO(std::unique_ptr<T> & var) {return RawIO(*var); }
    template<typename T> inline bool RawUPIOReg(std::unique_ptr<T> & var) {return RawIO_Register(*var); }
    template<typename T> inline bool AnonymousIO(T & var) { return IO(nullptr, var); }
    template<typename T> inline bool RawHandleIO(T *&ref) { return RawIOHandle(ref, UserContext); }
};



class ValueStringInterface
{
public:
    virtual ~ValueStringInterface() {}
    virtual bool Set(const std::string & val, std::string * errorMsg = nullptr) = 0;
    virtual const char * Get() const = 0;
};



template <typename enumtype, typename std::enable_if<std::is_enum<enumtype>::value>::type * = nullptr>
class EnumStringInterface : public ValueStringInterface
{
public:
    struct EnumItem {const char * Identifier; enumtype Value;};
    
public:
    enumtype & ValRef;
    
    inline EnumStringInterface(enumtype & valref) : ValRef(valref){}
    
    virtual bool Set(const std::string & val, std::string * errorMsg = nullptr) override;
    virtual const char * Get() const override;
};

namespace open { //users may add their specifications for their classes within this namespace
    template <typename enumtype, int nelems>
        using EnumOptionsList = std::array<typename EnumStringInterface<enumtype>::EnumItem, nelems>;
    template <typename enumtype> struct EnumOptions
    {
#ifndef _MSC_VER //Microsoft compiler doesn't understand this
        static constexpr EnumOptionsList<enumtype, 0> list = {};
#endif
    };
} //namespace open

template <typename enumtype, typename std::enable_if<std::is_enum<enumtype>::value>::type * typecheck>
bool EnumStringInterface<enumtype, typecheck>::Set(const std::string& val, std::string* errorMsg)
{
    constexpr auto Options = open::EnumOptions<enumtype>::list;
    static_assert(Options.size(), "Please write the EnumOptions specialisation for this enumeration type.");
    
    auto it = std::find_if(Options.begin(), Options.end(), 
                           [&val](const EnumItem & item){return (item.Identifier == val);});
    if(it != Options.end())
    {
        ValRef = it->Value;
        return true;
    }
    
    if(errorMsg)
    {
        auto it = begin(Options), itlast = end(Options);
        *errorMsg = "Invalid argument: '" + val + "'. Use " + it->Identifier;
        std::for_each(++it, --itlast, 
                      [errorMsg](const EnumItem & item){ (*errorMsg += ", ") += item.Identifier;});
        ((*errorMsg += " or ") += itlast->Identifier) += ".";
    }
    return false;
}

template <typename enumtype, typename std::enable_if<std::is_enum<enumtype>::value>::type * typecheck>
const char * EnumStringInterface<enumtype, typecheck>::Get() const
{
    constexpr auto Options = open::EnumOptions<enumtype>::list;
    static_assert(Options.size(), "Please write the EnumOptions specialisation for this enumeration type.");
    
    auto it = std::find_if(Options.begin(), Options.end(), 
                           [this](const EnumItem & item){return item.Value == ValRef; });
    assert(it != Options.end());
    return it->Identifier;
}


    
template<typename T, typename param_t, bool (LoadStore::*loadfun)(param_t), bool (LoadStore::*storefun)(param_t)>
bool LoadStore::IoHelper(const char * name, std::vector<T>& vec, bool required)
{
    if(name && !PrepareNamedVar(name, required)) return !required;
    
    bool ret;
    if(IsLoading())
    {
        assert(vec.empty());
        
        auto loadcallback = [&vec](LoadStore & ls)
        {
            vec.emplace_back();
            if(!(ls.*loadfun)(vec.back()))
            {
                vec.pop_back();
                return false;
            }
            return true;
        };
        ret = RawArrayIO(0, loadcallback);
    }
    else
    {
        auto iter = vec.begin();
        auto storecallback = [&iter](LoadStore & ls)
        {
            return (ls.*storefun)(*iter++);
        };
        
        ret = RawArrayIO(vec.size(), storecallback);
    }
    if(!ret) Error("while processing element %s", name);
    return FlushNamedVar(name, true) && ret;
}

template<typename T, typename param_t, bool (LoadStore::*loadfun)(param_t), bool (LoadStore::*storefun)(param_t)>
bool LoadStore::IoHelper(const char * name, Table<T>& table, bool required)
{
    if(name && !PrepareNamedVar(name, required)) return !required;
    
    bool ret;
    if(IsLoading())
    {
        assert(table.empty());
        
        auto loadcallback = [&table](std::string &key, LoadStore & ls)
        {
            T var;
            if(!(ls.*loadfun)(var)) return false;
            table.emplace(std::move(key), std::move(var));
            return true;
        };
        ret = RawMapIO(0, loadcallback);
    }
    else
    {
        auto iter = table.begin();
        auto storecallback = [&iter](std::string &key, LoadStore & ls)
        {
            key = iter->first;
            return (ls.*storefun)((iter++)->second);
        };
        
        ret = RawMapIO(table.size(), storecallback);
    }
    if(!ret) Error("while processing element %s", name);
    return FlushNamedVar(name, true) && ret;
}

template<typename T, typename std::enable_if<std::is_base_of<LoadStorableCompound, T>::value>::type*>
inline bool LoadStore::IO(const char * name, std::vector<T>& vec, bool required/* = true*/)
{
    return IoHelper<T, LoadStorableCompound&, &LoadStore::RawIO>(name, vec, required);
}

template<typename T, typename std::enable_if<std::is_base_of<LoadStorableCompound, T>::value>::type*>
inline bool LoadStore::IO(const char * name, std::vector<std::unique_ptr<T>>& vec, bool required/* = true*/)
{
    return IoHelper<std::unique_ptr<T>, std::unique_ptr<T>&, &LoadStore::RawUPIO>(name, vec, required);
}

template<typename T>
inline bool LoadStore::IO(const char * name, std::vector<std::vector<T>> & vec, bool required/* = true*/)
{
    return IoHelper<std::vector<T>, std::vector<T>&, &LoadStore::AnonymousIO>(name, vec, required);
}


template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type*>
inline bool LoadStore::IO_Register(const char * name, std::vector<T>& vec, bool required/* = true*/)
{
    return IoHelper<T, Referenceable&, &LoadStore::RawIO_Register>(name, vec, required);
}

template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type*>
inline bool LoadStore::IO_Register(const char * name, std::vector<std::unique_ptr<T>>& vec, bool required/* = true*/)
{
    return IoHelper<std::unique_ptr<T>, std::unique_ptr<T>&, &LoadStore::RawUPIOReg>(name, vec, required);
}

template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type*>
inline bool LoadStore::IO_Register(const char * name, graph::ContainerRange<graph::PresDeque<T>> range,
                                   bool required/* = true*/)
{
    assert(IsStoring() && "loading not supported");
    if(name && !PrepareNamedVar(name, required)) return !required;
    
    auto iter = range.begin();
    auto storecallback = [&iter](LoadStore & ls)
    {
        return ls.RawIO_Register(*iter++);
    };
    
    bool ret = RawArrayIO(range.size(), storecallback);
    if(!ret) Error("while processing element %s", name);
    return FlushNamedVar(name, true) && ret;
}

template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type*>
bool LoadStore::IORef(const char* name, std::vector< T* >& vec, bool required)
{
    return IoHelper<T*, T*&, &LoadStore::RawIORef>(name, vec, required);
}

template<typename T, typename std::enable_if<std::is_base_of<Referenceable, T>::value>::type*>
bool LoadStore::IOHandles(const char* name, std::vector< T* >& vec, void *pcontext, bool required)
{
    TemporaryContext ctx(*this, pcontext);
    return IoHelper<T*, T*&, &LoadStore::RawHandleIO>(name, vec, required);
}

}} //namespace Ladybirds::loadstore

#endif // LOADSTORE_H
