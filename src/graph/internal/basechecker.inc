#ifndef LADYBIRDS_GRAPH_INTERNAL_BASECHECKER_INC
#define LADYBIRDS_GRAPH_INTERNAL_BASECHECKER_INC

#include "../presdeque.h"


namespace Ladybirds {
namespace graph {
namespace internal {

template<typename t> class BaseChecker : public BaseCheckerBase
{
private:
    const PresDeque<t> * Base;
    typename PresDeque<t>::Size_t origsize;
public:
    BaseChecker(const PresDeque<t> * pbase) : Base(pbase), origsize(pbase->size()) {}
    virtual bool Check(const PresDequeElementBase * pelem) override
        { return Base->size() == origsize && Base->IsValidElement(pelem); }
    virtual bool Check(const BaseCheckerBase * pother) override
        {
            auto pbc = static_cast<const BaseChecker*>(pother);
            return Base->size() == origsize && pbc->Base == Base && pbc->origsize == origsize;
        }
};

}}} //namespace Ladybirds::graph::internal

#endif //ndef LADYBIRDS_GRAPH_INTERNAL_BASECHECKER_INC
