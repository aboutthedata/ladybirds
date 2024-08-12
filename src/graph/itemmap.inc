#include "presdeque.h"

#include "internal/basechecker.inc"

namespace Ladybirds {
namespace graph {


template<typename tval> template<typename t>
inline ItemMap<tval>::ItemMap(const PresDeque<t> & base, tval defaultval)
    : Vec_(base.GetMaxID()-base.GetMinID()+1, defaultval), MinID_(base.GetMinID())
{
    assert((BaseChecker_ = std::make_shared<internal::BaseChecker<t>>(&base)));
}

template<typename tval>
inline tval & ItemMap<tval>::operator [](const PresDequeElementBase & elem)
{
    assert(BaseChecker_ && BaseChecker_->Check(&elem));
    return Vec_[elem.GetID()-MinID_];
}


}} //namespace Ladybirds::graph
