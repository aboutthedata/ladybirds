// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "itemset.h"

#include <algorithm>

namespace Ladybirds { namespace graph {

void ItemSet::Insert(const PresDequeElementBase * pelem)
{
    assert(BaseChecker_ && BaseChecker_->Check(pelem));
    auto pos = pelem->GetID()-MinID_;
    Vec_[pos/wordsize] |= word(1) << (pos%wordsize);
}

void ItemSet::Remove(const PresDequeElementBase * pelem)
{
    assert(BaseChecker_ && BaseChecker_->Check(pelem));
    auto pos = pelem->GetID()-MinID_;
    Vec_[pos/wordsize] &= ~(word(1) << (pos%wordsize));
}


bool ItemSet::Contains(const PresDequeElementBase * pelem) const
{
    assert(BaseChecker_ && BaseChecker_->Check(pelem));
    auto pos = pelem->GetID()-MinID_;
    return (Vec_[pos/wordsize] & (word(1) << (pos%wordsize))) != 0;
}

void ItemSet::InsertAll()
{
    assert(BaseChecker_);
    Vec_.assign(Vec_.size(), ~word(0));
    ElementCountCorrection_ = ElementCountCorrectionBackup_;
}

void ItemSet::RemoveAll()
{
    assert(BaseChecker_);
    Vec_.assign(Vec_.size(), 0);
    ElementCountCorrection_ = 0;
}

std::size_t ItemSet::ElementCount() const
{
    assert(BaseChecker_);
    std::size_t bitcount = ElementCountCorrection_;
    for(word w : Vec_) bitcount += __builtin_popcount(w);
    return bitcount;
}

bool ItemSet::Contains(const ItemSet & other) const
{
    assert(BaseChecker_ && BaseChecker_->Check(other.BaseChecker_.get()));
    for(auto i = Vec_.size(); i-- > 0; )
    {
        if((Vec_[i] & other.Vec_[i]) != other.Vec_[i]) return false;
    }
    return true;
}

bool ItemSet::Intersects(const ItemSet & other) const
{
    assert(BaseChecker_ && BaseChecker_->Check(other.BaseChecker_.get()));
    for(auto i = Vec_.size(); i-- > 0; )
    {
        if((Vec_[i] & other.Vec_[i])) return true;
    }
    return false;
}


ItemSet & ItemSet::operator &=(const ItemSet & other)
{
    assert(BaseChecker_ && BaseChecker_->Check(other.BaseChecker_.get()));
    for(auto i = Vec_.size(); i-- > 0; ) Vec_[i] &= other.Vec_[i];
    return *this;
}

ItemSet & ItemSet::operator |=(const ItemSet & other)
{
    assert(BaseChecker_ && BaseChecker_->Check(other.BaseChecker_.get()));
    for(auto i = Vec_.size(); i-- > 0; ) Vec_[i] |= other.Vec_[i];
    return *this;
}

ItemSet & ItemSet::Remove(const ItemSet & other)
{
    assert(BaseChecker_ && BaseChecker_->Check(other.BaseChecker_.get()));
    for(auto i = Vec_.size(); i-- > 0; ) Vec_[i] &= ~other.Vec_[i];
    return *this;
}


}} //namespace Ladybirds::graph
