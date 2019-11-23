#include <stdlib.h> 
#include <stdint.h>
//#include "pf_interface.h"

uint32_t*
myIntMallocFn32(uint32_t sizeMalloc) 
{
	return (uint32_t*)malloc(sizeMalloc * sizeof(uint32_t));
}

uint64_t*
myIntMallocFn64(uint64_t sizeMalloc) 
{
	return (uint64_t*)malloc(sizeMalloc * sizeof(uint64_t));
}

void initialize(uint32_t * a, int size)
{
	for (int i = 0; i < size; ++i) {
		*(a + i) = i;
	}
}

void test(uint32_t * a, uint32_t * b, uint32_t *c)
{
	c[0] = a[0];
}

int main(/*int argc, char * argv[]*/)
{
  int size = 1000;
	uint32_t * a = myIntMallocFn32(size);
	uint32_t * b = myIntMallocFn32(size);
	uint32_t * c = myIntMallocFn32(size);
	uint32_t * d = myIntMallocFn32(size);
	uint64_t * e = (uint64_t*)myIntMallocFn64(size);
	int * f = (int*)malloc(size * sizeof(int));
	int * g = (int*)malloc(size * sizeof(int));

	// initialize data structures
	initialize(a,size);
	initialize(b,size);
	initialize(c,size);
	initialize(d,size);

	sim_roi_start();

	// indirection of the type A[B[i]]
	for(int i = 0; i < size; ++i) {
		*(f+i) = (uint64_t)b[a[i]];
	}

	for(int i = 0; i < size; ++i) {
		*(g+i) = (uint64_t)d[c[i]];
	}

	sim_roi_end();

	return 0;
}
