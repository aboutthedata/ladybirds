// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef OPT_COMMON__H
#define OPT_COMMON__H

#include <cstdint>

namespace Ladybirds { namespace opt {

using Time = int_least64_t;
static constexpr Time Time_Infinite = INT_LEAST64_MAX;
static constexpr Time Time_Invalid = INT_LEAST64_MIN;

}} // namespace Ladybirds::opt

#endif //ndef OPT_COMMON__H
