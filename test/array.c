#include <stdlib.h> 
#include <stdint.h>

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
    int size0 = 5, size1 = 5;
    int size2 = size0 + size1;
	//int * a = (int*)calloc(10, sizeof(int));
	//int * b = (int*)malloc(10 * sizeof(int));
	uint32_t * a = myIntMallocFn32(size2);
	uint32_t * b = myIntMallocFn32(10);
    uint64_t * c = (uint64_t*)myIntMallocFn64(10);
    int * d = (int*)malloc(10 * sizeof(int));

    // initialize data structures
	initialize(a,10);
	initialize(b,10);

    // indirection of the type A[B[i]]
	//int c = b[a[0]];
    for(int i = 0; i < 10; ++i) {
        *(c+i) = (uint64_t)b[a[i]];
    }

    return 0;
}
