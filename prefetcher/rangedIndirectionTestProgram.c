#include <stdlib.h> 
#include <stdint.h>
/*
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

int getFirstPtr(int* a, int i)
{
    return a[i];
}

int getSecondPtr(int* a, int i)
{
    return a[i+1];
}
*/

int main(/*int argc, char * argv[]*/)
{
    /*
     * single valued indirection
     *
    int size0 = 5, size1 = 5;
    int size2 = size0 + size1;
	uint32_t * a = myIntMallocFn32(size2);
	uint32_t * b = myIntMallocFn32(10);
	uint32_t * c = myIntMallocFn32(10);
    uint64_t * d = (uint64_t*)myIntMallocFn64(10);
    //int * d = (int*)malloc(10 * sizeof(int));
	initialize(a,10);
	initialize(b,10);
	initialize(c,10);
    for(int i = 0; i < 10; ++i) {
        d[i] = (uint64_t)c[b[a[i]]];
        //d[i] = (uint64_t)c[a[i]];
        //c[a[i]] = (uint64_t)b[i];
    }
    */

    /* ranged indirection 
     */
    int a[4] = {0, 4, 8, 12};
    int b[12];
    for(int i = 0; i < 12; ++i) {
        b[i] = i;
    }
    for(int i = 0; i < 4; ++i) {
        int sum = 0;
        for(int j = a[i]; j < a[i+1]; ++j) {
        //for(int j = getFirstPtr(a,i); j < getSecondPtr(a,i); ++j) {
            sum += b[j];
        }
    }
    //*/

    return 0;
}
