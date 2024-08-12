// Copyright ETH Zurich, 2022. This code is under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.

#ifndef CONSTANTS_H
#define CONSTANTS_H


#define SOUND_SPEED 1430

#define USFREQ      (7.5e6)
#define PITCH       (245e-6)
#define TIMESTEP    (20e-9)

#define ATTENUATION_DB 1 /* [dB/cm] */



#define M_PI        3.14159265358979323846


#define TRUE 1
#define FALSE 0

#define min(a, b) ( ( (a) < (b) ) ? (a) : (b) )
#define max(a, b) ( ( (a) > (b) ) ? (a) : (b) )

#endif

