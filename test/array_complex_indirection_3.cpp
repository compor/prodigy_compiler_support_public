#include <stdlib.h> 
#include <stdint.h>
//#include "pf_interface.h"
#include <vector>
#include <stdio.h>

class ARR
{
private:
  std::vector<uint32_t> val;
  
public:
  
  ARR(int size) {
    val = std::vector<uint32_t>(size);
  }

  void set(int index) {
    val.at(index) = index;
  }
  
  int get(int index) {
    return val.at(index);
  }

  int size() {
    return val.size();
  }
};


class Graph
{
private:
  std::vector<ARR*> nodes;
  
public:
  Graph() {
    //nodes = new std::vector<ARR*>;
  }
  
  void assign_node(ARR* a) {
    nodes.push_back(a);
  }

  uint32_t getNodeIVal(uint32_t node_id, uint32_t arr_id)
  {
    return nodes.at(node_id)->get(arr_id);
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

int access_3(Graph & g, uint32_t * b, uint32_t * c, uint32_t * d, int size, uint32_t node_id, uint32_t arr_id)
{
    // indirection of the type A[B[i]]
    for(int i = 0; i < 10; ++i) {
      *(d+i) = c[b[g.getNodeIVal(node_id,arr_id)]] + 1;
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
    Graph g = Graph();
    g.assign_node(&arr);
    
    // initialize data structures
    initialize(a,10);
    initialize(b,10);
    initialize(c,10);
    initialize_arr(arr);

    int ret = access_3(g,b,c,d,size2,0,0);
    
    return ret;
}
