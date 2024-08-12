#ifndef LADYBIRDS_H_
#define LADYBIRDS_H_

#ifdef __LADYBIDRS_PARSER_AT_WORK__

#ifdef __cplusplus
#define _LB_PROTO ...
#else
#define _LB_PROTO
#endif

void invoke(int);
void invokeseq(int);
#define kernel(x) int x(_LB_PROTO); void _lb_kernel_##x
#define metakernel(x) int x(_LB_PROTO); void _lb_metakernel_##x
#define in __attribute__((annotate("in"))) const
#define out __attribute__((annotate("out")))
#define inout __attribute__((annotate("inout")))
#define buddy(buddypacket) __attribute__((annotate("buddy="#buddypacket)))
#define param __attribute__((annotate("param"))) const
#define genvar __attribute__((annotate("genvar")))

#else //def __LADYBIDRS_PARSER_AT_WORK__

#define kernel(x) void x
#define metakernel(x) void x
#define in const
#define out
#define inout
#define buddy(buddypacket)
#define param const
#define genvar
#define invoke(x) (x)
#define invokeseq(x) (x)

#endif //def __LADYBIDRS_PARSER_AT_WORK__

#if defined(__GNUC__) && !defined(__clang__)
#define _LB_HIDDEN(x) 0
#else
#define _LB_HIDDEN(x) x
#endif


void fromfile(void * data, int size, const char * filename);


#endif //LADYBIRDS_H_
