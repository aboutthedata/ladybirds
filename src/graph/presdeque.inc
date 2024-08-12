#include "itemset.h"

namespace Ladybirds {namespace graph {

template<typename t> ItemSet PresDeque<t>::GetSubset(bool full) const { return ItemSet(*this, full); }
}} //namespace Ladybirds::graph

