#include <stdlib.h> 
#include <stdint.h>
//#include "pf_interface.h"

void initialize(uint32_t * a, int size)
{
  for (int i = 0; i < size; ++i) {
    *(a + i) = i;
  }
}

int access(uint32_t * a, uint32_t * b, uint32_t * c, uint32_t * d, int size)
{
    // indirection of the type A[B[i]]
    for(int i = 0; i < 10; ++i) {
        *(d+i) = c[b[a[i]]] + 1;
    }
    return 0;
}

int main(/*int argc, char * argv[]*/)
{
    int size0 = 5, size1 = 5;
    int size2 = size0 + size1;
    uint32_t * a = (uint32_t*)malloc(size2*4);
    uint32_t * b = (uint32_t*)malloc(10*4);
    uint32_t * c = (uint32_t*)malloc(10*4);
    uint32_t * d = (uint32_t*)malloc(10*4);

    // initialize data structures
    initialize(a,10);
    initialize(b,10);
    initialize(c,10);

    int ret = access(a,b,c,d,size2);
    return ret;
}
