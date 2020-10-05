#include <stdlib.h> 
#include <stdint.h>
//#include "pf_interface.h"
#include <vector>
#include <stdio.h>

class ARR
{
private:
  std::vector<char> *val;
  
public:
  
  ARR(int size) {
    val = new std::vector<char>(size);
  }

  void set(int index) {
    val->at(index) = index;
  }
  
  int get(int index) {
    return val->at(index);
  }

  int size() {
    return val->size();
  }
};


void initialize(uint32_t * a, int size)
{
  for (int i = 0; i < size; ++i) {
    *(a + i) = i;
  }
}

void initialize_arr(ARR & arr)
{
  for (int i = 0; i < arr.size(); ++i) {
    arr.set(i);
  }
}

int access_3(ARR & arr, uint32_t * b, uint32_t * c, uint32_t * d, int size)
{
    // indirection of the type A[B[i]]
    for(int i = 0; i < 10; ++i) {
      *(d+i) = c[b[arr.get(i)]] + 1;
      *(d+i) = c[b[arr.get(i)]] + 1;
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

    ARR arr = ARR(10);
    
    // initialize data structures
    initialize(a,10);
    initialize(b,10);
    initialize(c,10);
    initialize_arr(arr);

    int ret = access_3(arr,b,c,d,size2);
    
    return ret;
}
