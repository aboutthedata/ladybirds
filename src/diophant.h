// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef DIOPHANTINE_H
#define DIOPHANTINE_H

#include <type_traits>

#include "range.h"


namespace Ladybirds{ namespace diophant {

//! Returns \p a modulo \p b. The result is always within the range {0 ... b-1} unlike with the % operator.
template<typename t> inline t Modulo(t a, t b)
{
    static_assert(std::is_integral<t>(), "Only implemented for integer types");
    t remainder = a % b;
    return remainder < 0 ? remainder + b : remainder;
}

//! Returns the greatest common divisor of two integers
int Gcd(int i1, int i2);
    
//! Returns true iff two periodically recurring windows overlap at any point in time
/** There are two periods of lengths \p period1 and \p period2; they start together at 0 if \p offset == 0,
 *  otherwise \p period2 starts at offset.
 *  Within these periods, the ranges \p wnd1 and \p wnd2 define "windows" that open and close.
 *  PeriodicWindowOverlap now returns true if at any point between 0 and infinity, both windows are open simultaneously.
 **/
bool PeriodicWindowOverlap(gen::Range wnd1, int period1, gen::Range wnd2, int period2, int offset = 0);


}} //namespace Ladybirds::gen

#endif // DIOPHANTINE_H
