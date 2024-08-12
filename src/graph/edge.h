// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef LADYBIRDS_GRAPH_EDGE_H
#define LADYBIRDS_GRAPH_EDGE_H

#include "presdeque.h"

namespace Ladybirds {
namespace graph {



template<class NodeClass>
class Edge : public PresDequeElementBase
{
    using node_t = NodeClass;
    template<class, class> friend class Graph;
    template<class> friend class EdgeIterator;
    template<class> friend class OutEdgeIterator;
    template<class> friend class InEdgeIterator;

private:
    node_t *Source_ = nullptr, *Target_ = nullptr;
    typename node_t::edge_t *PrevIn_ = nullptr, *NextIn_ = nullptr, *PrevOut_ = nullptr, *NextOut_ = nullptr;

protected:
    inline Edge(const Edge&) {};
    inline Edge(Edge &&) = default;
    Edge & operator =(const Edge&) = delete; //by default
    Edge & operator =(Edge&&) = default;
    
public:
    inline Edge() {};
    virtual ~Edge() {}
    
    inline node_t *GetSource()             { return Source_;}
    inline const node_t *GetSource() const { return Source_;}
    inline node_t *GetTarget()             { return Target_;}
    inline const node_t *GetTarget() const { return Target_;}
};


template<typename edge_t>
class OutEdgeIterator
{
    template<class t1, class t2> friend class Node;
private:
    edge_t * p_;
    
public:
    inline constexpr OutEdgeIterator()           : p_(nullptr) {} //End iterator
    inline OutEdgeIterator(const OutEdgeIterator&) = default;
    inline OutEdgeIterator(OutEdgeIterator&&) = default;
    inline OutEdgeIterator & operator=(const OutEdgeIterator&) = default;
    inline OutEdgeIterator & operator=(OutEdgeIterator&&) = default;
    
private:
    //Called by node class
    inline constexpr OutEdgeIterator(edge_t * p) : p_(p) {}       //Begin iterator
    
public:
    inline bool operator==(const OutEdgeIterator & it) { return it.p_ == p_; }
    inline bool operator!=(const OutEdgeIterator & it) { return it.p_ != p_; }
    
    inline edge_t & operator*()  { return *p_; }
    inline edge_t * operator->() { return p_; }
    inline OutEdgeIterator & operator++() { p_ = p_->NextOut_; return *this; }
    inline OutEdgeIterator operator++(int) { OutEdgeIterator it = *this; ++*this; return it;}
};

template<typename edge_t>
class InEdgeIterator
{
    template<class t1, class t2> friend class Node;
private:
    edge_t * p_;
    
public:
    inline constexpr InEdgeIterator()           : p_(nullptr) {} //End iterator
    inline InEdgeIterator(const InEdgeIterator&) = default;
    inline InEdgeIterator(InEdgeIterator&&) = default;
    inline InEdgeIterator & operator=(const InEdgeIterator&) = default;
    inline InEdgeIterator & operator=(InEdgeIterator&&) = default;
    
private:
    //Called by node class
    inline constexpr InEdgeIterator(edge_t * p) : p_(p) {}       //Begin iterator
    
public:
    inline bool operator==(const InEdgeIterator & it) { return it.p_ == p_; }
    inline bool operator!=(const InEdgeIterator & it) { return it.p_ != p_; }
    
    inline edge_t & operator*()  { return *p_; }
    inline edge_t * operator->() { return p_; }
    inline InEdgeIterator & operator++() { p_ = p_->NextIn_; return *this; }
    inline InEdgeIterator operator++(int) { InEdgeIterator it = *this; ++*this; return it;}
};

template<typename edge_t>
class EdgeIterator
{
    template<class t1, class t2> friend class Node;
private:
    edge_t *pin_, *pout_;

public:
    inline constexpr EdgeIterator()                         : pin_(nullptr), pout_(nullptr) {} //End iterator
    inline EdgeIterator(const EdgeIterator&) = default;
    inline EdgeIterator(EdgeIterator&&) = default;
    inline EdgeIterator & operator=(const EdgeIterator&) = default;
    inline EdgeIterator & operator=(EdgeIterator&&) = default;
    
private:
    //Called by node class
    inline constexpr EdgeIterator(edge_t * pin, edge_t * pout) : pin_(pin), pout_(pout) {}     //Begin iterator
    
public:
    inline bool operator==(const EdgeIterator & it) { return it.pin_ == pin_ && it.pout_ == pout_; }
    inline bool operator!=(const EdgeIterator & it) { return it.pin_ != pin_ || it.pout_ != pout_; }
    
    inline edge_t * operator->() { return pin_ ? pin_ : pout_; }
    inline edge_t & operator*()  { return *operator->(); }
    
    EdgeIterator & operator++()
    {
        if(pin_)
        {
            pin_ = pin_->NextIn_;
            return *this;
        }
        else do
        {
            pout_ = pout_->NextOut_;
        }
        while (pout_ && pout_->Source_ == pout_->Target_); //Skip "self edges": these have already been handled as input edges.
        return *this;
    }
    inline EdgeIterator operator++(int) { EdgeIterator it = *this; ++*this; return it;}
};

template<class iterator>
class ItRange
{
private:
    iterator it_;
    
public:
    inline ItRange(iterator it) : it_(it) {};
    
    inline iterator begin() const { return it_; }
    inline iterator end() const { return iterator(); }
};
template<class iterator> auto GetItRange(iterator it) { return ItRange<iterator>(it); }


}} //namespace Ladybirds::graph

#endif // LADYBIRDS_GRAPH_EDGE_H
