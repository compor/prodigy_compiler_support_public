/*
 * pf_c_interface.h
 *
 *  Created on: 7 Nov 2019
 *      Author: kuba
 */

#ifndef INCLUDE_PF_C_INTERFACE_H_
#define INCLUDE_PF_C_INTERFACE_H_

//#include <pf_interface.h>
//#include <sim_api.h>
#include <stdint.h>
#include <vector>
#include <stdio.h>

typedef int64_t NodeId;

enum FuncId
{
    // traversal functions registered
    TraversalHolder,
    BaseOffset_int32_t,
    PointerBounds_int32_t,
    PointerBounds_uint64_t,

    // trigger functions registered
    TriggerHolder,
    UpToOffset,
    StaticOffset_32,
    StaticOffset_64,
    StaticOffset_256,
    StaticOffset_512,
    StaticOffset_1024,

    // squash functions registered
    SquashIfLarger,
    NeverSquash,

    InvalidFuncId
};

extern "C" {

int create_params(int num_nodes_pf, int num_edges_pf, int num_triggers_pf);
int create_enable();
int register_node(void *base, int64_t size, int64_t node_id);
int register_node_with_size(void *base, int64_t size, int64_t elem_size,
                            int64_t node_id);
int register_trav_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to,
                       FuncId f);
int register_trav_edge2(NodeId id_from, NodeId id_to, FuncId f);
int register_trig_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f,
                       FuncId sq_f);
int register_trig_edge2(NodeId id_from, NodeId id_to, FuncId f, FuncId sq_f);
int sim_user_pf_set_param();
int sim_user_pf_set_enable();
int sim_user_pf_enable();
int sim_user_wait();
int sim_roi_start();
int sim_roi_end();
int sim_user_pf_disable();
int delete_params();
int delete_enable();

// Profile
int register_identify_edge(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f);
int register_identify_edge_source(uintptr_t baseaddr_from, int edge_id);
int register_identify_edge_target(uintptr_t baseaddr_to, int edge_id);


class profiler_node_int
{
public:
    profiler_node_int(uintptr_t _base, int64_t _size, int64_t _type_size, NodeId _id)
    : base(_base)
    , size(_size)
    , type_size(_type_size)
    , id(_id)
    { }

    uintptr_t base;
    int64_t size;
    int64_t type_size;
    NodeId id;
};

class profiler_edge_int
{
public:
    profiler_edge_int(NodeId _from, NodeId _to, FuncId _f)
    : from(_from)
    , to(_to)
    , f(_f)
    { }

    NodeId from;
    NodeId to;
    FuncId f;

    void Print() const
    {
        printf("(%ld, %ld, %d)", from, to, f);
    }
};

std::vector<profiler_node_int> nodes;

uint64_t
GetNodeIdFromBaseAddr(uint64_t baseaddr_from)
{
    for(uint64_t i = 0; i < nodes.size(); ++i) {
//    	fprintf(stderr, "%d %d %d\n", baseaddr_from, (uint64_t)(nodes[i].base), ((uint64_t)nodes[i].base)+nodes[i].size*nodes[i].type_size);
        if(baseaddr_from < ((uint64_t)nodes[i].base)+nodes[i].size*nodes[i].type_size && baseaddr_from >= (uint64_t)(nodes[i].base)) {
//        	fprintf(stderr, "Node found.\n");
            return i;
        }
    }
    return 0;
}

int register_identify_edge_source(uintptr_t baseaddr_from, int edge_id)
{
	int err = 0;

	uint64_t from = GetNodeIdFromBaseAddr(baseaddr_from);
	fprintf(stderr, "PROFILE SOURCE %d, edge %d\n", from, edge_id);

	return err;
}

int register_identify_edge_target(uintptr_t baseaddr_to, int edge_id)
{
	uint64_t to = GetNodeIdFromBaseAddr(baseaddr_to);
	fprintf(stderr, "PROFILE TARGET %d, edge %d\n", to, edge_id);
	return 0;
}


int
print_params()
{
	return 0;
}

int create_params(int num_nodes_pf, int num_edges_pf, int num_triggers_pf)
{

	return 0;
}

int create_enable()
{
	return 0;
}

int register_node_with_size(void* base, int64_t size, int64_t elem_size, int64_t node_id)
{
	int err = 0;

	fprintf(stderr, "%s\n", __FUNCTION__);
    nodes.push_back(profiler_node_int((uintptr_t)base, size, elem_size, node_id));

	return err;
}

int
register_trav_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f)
{
	return 0;
}

int
register_trav_edge2(NodeId id_from, NodeId id_to, FuncId f)
{
	return 0;
}

int register_trig_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f, FuncId sq_f)
{
	return 0;
}

int register_trig_edge2(NodeId id_from, NodeId id_to, FuncId f, FuncId sq_f)
{
	return 0;
}

int sim_user_pf_set_param()
{
	return 0;
}

int sim_user_pf_set_enable()
{
	return 0;
}

int sim_user_pf_enable()
{
	return 0;
}

int
pf_delete_trav(uintptr_t baseaddr_from, uintptr_t baseaddr_to)
{
	return 0;
}

int
pf_clear_trav()
{
	return 0;
}

int pf_delete_trig(uintptr_t baseaddr_from, uintptr_t baseaddr_to)
{
	return 0;
}

int pf_clear_trig()
{
	return 0;
}

int sim_user_wait()
{
	int err = 0;

	return err;
}

int sim_roi_start()
{
	return 0;
}

int sim_roi_end()
{
	return 0;
}

int sim_user_pf_disable()
{
	return 0;
}

int delete_params()
{
	return 0;
}

int delete_enable()
{
	return 0;
}

} // extern "C"

#endif /* INCLUDE_PF_C_INTERFACE_H_ */
