// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#include "diophant.h"

namespace Ladybirds{ namespace diophant {

using gen::Range;

int Gcd(int i1, int i2)
{
    assert(i1 > 0 && i2 > 0);
    while(true)
    {
        if((i1 %= i2) == 0) return i2;
        if ((i2 %= i1) == 0) return i1;
    }
}

    
bool PeriodicWindowOverlap(Range wnd1, int period1, Range wnd2, int period2, int offset)
{
    assert(Range::BeginEnd(0, period1).Contains(wnd1));
    assert(Range::BeginEnd(0, period2).Contains(wnd2));
    
    int gcd = Gcd(period1, period2);
    int lb = wnd1.begin() - wnd2.end() - offset;
    int ub = wnd1.end() - wnd2.begin() - offset;
    
    return Modulo(lb, gcd) + (ub-lb) > gcd;
}



}} //namespace Ladybirds::diophant
