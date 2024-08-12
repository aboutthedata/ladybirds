// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef GRAPH_PRESDEQUE_H
#define GRAPH_PRESDEQUE_H

#include <cassert>
#include <deque>
#include <iostream>
#include <utility>

namespace Ladybirds {namespace graph {

template<typename t> class PresDeque;
template<class base> class PresDequeIterator;
class ItemSet;

struct PresDequeElementBase
{
    using ID_t = std::ptrdiff_t;
    template<typename t> friend class PresDeque;
    template<typename t> friend class PresDequeIterator;
private:
    ID_t ID_ = 0, PrevID_ = -1LL;
    inline bool IsAlive() const { return PrevID_ < 0; }
    
public:
    inline PresDequeElementBase() = default;
    inline PresDequeElementBase(const PresDequeElementBase &) {}  //the ID_ and the PrevID_ fields are changed...
    inline PresDequeElementBase(PresDequeElementBase &&) {}       // ...by PresDeque and no one else
    inline PresDequeElementBase & operator =(const PresDequeElementBase &) { return *this; } //dito
    inline PresDequeElementBase & operator =(PresDequeElementBase &&) { return *this; } //dito
    
    inline auto GetID() const { assert(IsAlive()); return ID_; }
};


/// \internal Iterator class for PresDeque
template<class base>
class PresDequeIterator : public base
{
public:
    using pointer = typename std::iterator_traits<base>::pointer;
    using reference = typename std::iterator_traits<base>::reference;
    
    PresDequeIterator() = default;
    
    using base::operator++; //Checking for validity will be done when referencing.
    // TODO: This does not work for multiple subsequent calls to the ++ operator. Find a better solution.
    auto operator--() //Find previous valid object.
    {
        while(!base::operator--().base::operator*().IsAlive());
        return *this;
    }
    auto operator--(int) { auto & ret = *this; --*this; return ret; }
    
    /// Like + operator, but only works when there are no gaps in the list.
    PresDequeIterator UnsafeAdvance(int i)
    {
        return base::operator+(i);
    }
    
    reference operator*() //Find next valid object and return it. We can only do this here, because we would be running
    {//past the end of the deque in the ++ operator if we were already at the last element. Note that the last element
     //in the deque will always be valid since it would otherwise be removed from the deque
        pointer p;
        while(!(p = &base::operator*())->IsAlive()) ++*this;
        return *p;
    }
    
    pointer operator->() { return &**this; }
    
private:
    friend class PresDeque<typename std::iterator_traits<base>::value_type>;
    PresDequeIterator(const base &it) : base(it) {}
    PresDequeIterator(base &&it) : base(std::move(it)) {}
};

template<class container>
class ContainerRange
{
private:
    container & Ctr_;
    
public:
    inline ContainerRange(container & ctr) : Ctr_(ctr) {}
    auto inline begin() { return Ctr_.begin(); }
    auto inline end()   { return Ctr_.end();   }
    
    auto inline empty() const { return Ctr_.empty(); }
    auto inline size() const { return Ctr_.size(); }
    auto inline count() const { return Ctr_.size(); }
};
template<class container> auto GetRange(container & ctr) { return ContainerRange<container>(ctr); }


/// Helper class that prevents std containers from calling constructors or destructors of their elements.
/// This class is needed by PresDeque, which has a different construction/destruction scheme and thus handles
/// construction and destruction "manually".
template<typename t>
class NonXstructiveAllocator : public std::allocator<t>
{
public:
    template<class... Args> void construct(t*, Args&&...) {} //No construction!
    template<class U, class... Args> void construct( U* p, Args&&... args )
        {::new((void *)p) U(std::forward<Args>(args)...);} //but normal construction of any other object
    void destroy(t*) {}; //No destruction!
    template<class U> void destroy(U* p) {p->~U();} //but normal destruction of any other object
    template<class U> struct rebind
    {//this is necessary because STL uses rebind to get an allocator for type t (again!)
        using other = std::conditional_t<std::is_same<U, t>::value, NonXstructiveAllocator<t>, std::allocator<U>>;
    };
};

/// A container class that does not provide any order of objects, but guarantees lifetime pointer validity for all of
/// its elements, independently of any add/remove operations. Efficient memory usage is achieved by using std::deque
/// for storage management. Deleting items in the list, however, does not result in any moving operations or 
/// reallocations. Instead, the space is marked as free and will be reused when a new object is added.
/// Objects to be managed by PresDeque must have an implementation of graph::open::Valid and graph::open::NextSlot.
template<typename t> class PresDeque : private std::deque<t, NonXstructiveAllocator<t>>
{
    using base = std::deque<t, NonXstructiveAllocator<t>>;
    
public:
    using ID_t = PresDequeElementBase::ID_t;
    using typename base::size_type;
    using Size_t = size_type;
    using iterator = PresDequeIterator<typename base::iterator>;
    using const_iterator = PresDequeIterator<typename base::const_iterator>;
    
private:
    ID_t FirstSlot_ = 0, LastSlot_ = 0;
    ID_t MinID_ = 1, MaxID_ = 0;
    Size_t Size_ = 0;
    
public:
    PresDeque() : base() {};
    PresDeque(const PresDeque &) = delete; //TODO: implement copy constructor as well as assignment
    PresDeque & operator=(const PresDeque &) = delete;
    PresDeque(PresDeque &&) = default;
    PresDeque & operator=(PresDeque&&) = default;
    
    ~PresDeque() { clear(); }
    
    t & FromID(ID_t id) { auto it = IteratorFromID(id); assert(it->IsAlive()); return *it; }
        
    inline auto GetMinID() const { return MinID_; }
    inline auto GetMaxID() const { return MaxID_; }
    
    inline auto size() const { return Size_; }
    
    iterator begin() {return iterator(base::begin());}
    const_iterator begin() const {return base::begin();}
    const_iterator cbegin() const {return base::cbegin();}
    iterator end() {return base::end();}
    const_iterator end() const {return base::end();}
    const_iterator cend() const {return base::cend();}
    
    using base::front;
    using base::back;
    using base::empty;
    
    void clear()
    {
        for(t & obj : *this) obj.~t();
        base::clear();
        FirstSlot_ = LastSlot_ = Size_ = 0;
        MinID_ = 1; MaxID_ = 0;
    }
    
    /// Returns an ItemSet with this list as a base.
    /// The returned set contains all elements in this list if \p full is true, none otherwise.
    ItemSet GetSubset(bool full = false) const;
    
    iterator insert(const t & value)
    {
        ++Size_;
        ID_t id;
        auto it = GetSlot(id);
        new(&*it) t(value);
        it->ID_ = id;
        return it;
    }

    iterator insert(t && value)
    {
        ++Size_;
        ID_t id;
        auto it = GetSlot(id);
        new(&*it) t(value);
        it->ID_ = id;
        return it;
    }

    template< class... Args >
    iterator emplace( Args&&... args )
    {
        ++Size_;
        ID_t id;
        auto it = GetSlot(id);
        new(&*it) t(std::forward<Args>(args)...);
        it->ID_ = id;
        return it;
    }
    
    iterator erase(t * p)
    {
        --Size_;
        auto id = p->ID_;
        auto it = base::begin() + (id - GetMinID());
        assert(&*it == p && "trying to remove an element which is not part of the list");

        p->~t();
        
        auto cleanelem = [this](t & elem)
        {
            if(empty()) { MinID_ = 1; MaxID_ = 0; return false; }
            if(elem.IsAlive()) return false;
            
            auto previd = elem.PrevID_, nextid = elem.ID_;
            if(previd) IteratorFromID(previd)->ID_ = nextid;
            else FirstSlot_ = nextid;
            if(nextid) IteratorFromID(nextid)->PrevID_ = previd;
            else LastSlot_ = previd;
            return true;
        };

        if(id == GetMinID()) 
        {
            do { base::pop_front(); ++MinID_ ; }
            while(cleanelem(front()));
            return begin();
        }
        if(id == GetMaxID())
        {
            do { base::pop_back(); --MaxID_ ; }
            while(cleanelem(back()));
            return end();
        }
        
        p->ID_ = 0;
        p->PrevID_ = LastSlot_;

        if(LastSlot_ == 0) FirstSlot_ = id;
        else IteratorFromID(LastSlot_)->ID_ = id;
        LastSlot_ = id;
        return ++it;
    }
    
    bool IsValidElement(const PresDequeElementBase * pelem) const
    {
        return pelem->IsAlive() && pelem->GetID() <= GetMaxID() &&
            &*IteratorFromID(pelem->GetID()) == static_cast<const t*>(pelem);
    }
    
    void Dump() const
    {
        std::cout << FirstSlot_ << " : " << LastSlot_ << " –";
        for(auto it = base::begin(), itend = base::end(); it != itend; ++it)
        {
            std::cout << " (" << it->PrevID_ << ',' << it->ID_ << ')';
        }
        std::cout << std::endl;
    }
    
private:
    inline typename base::iterator IteratorFromID(ID_t id)
    {
        assert(id <= MaxID_ && id >= MinID_);
        return base::begin() + (id - MinID_);
    }

    inline typename base::const_iterator IteratorFromID(ID_t id) const
    {
        assert(id <= MaxID_ && id >= MinID_);
        return base::begin() + (id - MinID_);
    }
    
    typename base::iterator GetSlot(/*out*/ ID_t &id)
    {
        if(!FirstSlot_)
        {
            base::emplace_back();
            id = ++MaxID_;
            return base::end()-1;
        }
        
        id = FirstSlot_;
        auto newslot = IteratorFromID(id);

        auto nextslotid = newslot->ID_;
        FirstSlot_ = nextslotid;
        if(nextslotid) IteratorFromID( nextslotid )->PrevID_ = 0;
        else LastSlot_ = 0;
        
        return newslot;
    }
};

}} //namespace Ladybirds::graph

#include "presdeque.inc"


#endif // GRAPH_PRESDEQUE_H
