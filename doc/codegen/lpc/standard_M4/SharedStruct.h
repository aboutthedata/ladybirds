#ifndef SHARED_STRUCT_H
#define SHARED_STRUCT_H

//needed for uint8_t
#include "board.h"

struct struct_shared_variables {
	volatile uint8_t* mapping_vector;
	volatile uint8_t* arrival_vector;
	volatile uint8_t* execution_vector;

	volatile int numberOfTasksinTotal;

	volatile int m4_ready;
	volatile int m0_ready;

	volatile int m4_assigned_taskcounter;
	volatile int m0_assigned_taskcounter;

	//volatile int* shared_array;

	volatile uint8_t** buffer_vector;

};

#endif
