#include "presdeque.h"

#include "internal/basechecker.inc"

namespace Ladybirds {
namespace graph {


template<typename t> inline ItemSet::ItemSet(const PresDeque<t> & base, bool allin)
    : Vec_((base.GetMaxID()-base.GetMinID()+wordsize) / wordsize, allin ? ~word(0) : 0),
      MinID_(base.GetMinID())
{
    assert((BaseChecker_ = std::make_shared<internal::BaseChecker<t>>(&base)));
    ElementCountCorrectionBackup_ = -(Vec_.size()*wordsize - base.size()); //will be negative (overflow)
    ElementCountCorrection_ = allin ? ElementCountCorrectionBackup_ : 0;
}


}} //namespace Ladybirds::graph
