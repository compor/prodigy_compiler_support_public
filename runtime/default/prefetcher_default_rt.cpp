/*
BSD 3-Clause License

Copyright (c) 2021, Kuba Kaszyk and Chris Vasiladiotis
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef INCLUDE_PF_C_INTERFACE_H_
#define INCLUDE_PF_C_INTERFACE_H_

#include <pf_interface.h>
#include <sim_api.h>
#include <vector>

extern "C" {

int create_params(int num_nodes_pf, int num_edges_pf, int num_triggers_pf);
int create_enable();
int register_node(void *base, int64_t size, int64_t node_id);
int register_node_with_size(uintptr_t base, int64_t size, int64_t elem_size,
                            int64_t node_id);
int register_trav_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to,
                       FuncId f, int id);
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
void c_print_verify();

// Profile
int register_identify_edge(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f);
int register_identify_edge_source(uintptr_t baseaddr_from, int edge_id);
int register_identify_edge_target(uintptr_t baseaddr_to, int edge_id);


pf_params_t * params;
pf_enable_t * enable;

int
print_params()
{
	params->Print();
	return 0;
}

int create_params(int num_nodes_pf, int num_edges_pf, int num_triggers_pf)
{
	int params_id = 0;
	params = new pf_params_t(num_nodes_pf, num_edges_pf, num_triggers_pf, 1); // KUBA CHANGE THIS: Update last parameter to reflect cores
	printf("****pf: &params = %p %d %d %d\n", params, num_nodes_pf, num_edges_pf, num_triggers_pf);

	return params_id;
}

int create_enable()
{
	int enable_id = 0;
	enable = new pf_enable_t();
	printf("****pf: &enable = %p\n", enable);

	return enable_id;
}
//
//template <typename T>
//int register_node(T* base, int64_t size, int64_t node_id)
//{
//	int err = 0;
//
//	params->RegisterNode(base, size, node_id);
//
//	return err;
//}

int register_node_with_size(uintptr_t base, int64_t size, int64_t elem_size, int64_t node_id)
{
	int err = 0;

	params->RegisterNodeWithSize(base, size, elem_size, node_id);

	return err;
}


__attribute__ ((noinline))
int
register_trav_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f, int id)
{
	int err = 0;

	(void) params->RegisterTravEdge(baseaddr_from, baseaddr_to, f, id);

	return err;
}

int
register_trav_edge2(NodeId id_from, NodeId id_to, FuncId f)
{
	int err = 0;

	(void) params->RegisterTravEdge(id_from, id_to, f);

	return err;
}

int register_trig_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f, FuncId sq_f)
{
	int err = 0;

	params->RegisterTrigEdge(baseaddr_from, baseaddr_to, f, sq_f);

	return err;
}

int register_trig_edge2(NodeId id_from, NodeId id_to, FuncId f, FuncId sq_f)
{
	int err = 0;

	params->RegisterTrigEdge(id_from, id_to, f, sq_f);

	return err;
}

int sim_user_pf_set_param()
{
	int err = 0;

	SimUser(PF_SET_PARAM, (long unsigned int) params);
	printf("pf: &params = %p\n", params);

	return err;
}

int sim_user_pf_set_enable()
{
	int err = 0;

	SimUser(PF_SET_ENABLE, (long unsigned int) enable);
	printf("pf: &enable = %p\n", enable);

	return err;
}

int sim_user_pf_enable()
{
	int err = 0;

	SimUser(PF_ENABLE, (long unsigned int) enable);
	printf("pf: &enable = %p\n", enable);

	return err;
}

/**
 * @brief Removes the traversal edge between the two defined
 *        nodes in the DIG
 *        NOTE: This only removes from the application representation
 *              and requires a sim_user_pf_set_param() call to
 *              push the changes to the simulator
 * @param baseaddr_from Base addr of the from node
 * @param baseaddr_to Base addr of the to node
 * @retval Int 0 on success
 */
int
pf_delete_trav(uintptr_t baseaddr_from, uintptr_t baseaddr_to)
{
	params->DeleteTravEdge(baseaddr_from, baseaddr_to);
	return 0;
}

/**
 * @brief Removes the all traversal edges from DIG
 *        NOTE: This only removes from the application representation
 *              and requires a sim_user_pf_set_param() call to
 *              push the changes to the simulator
 * @retval Int 0 on success
 */
int
pf_clear_trav()
{
	params->ClearTravEdges();
	return 0;
}

/**
 * @brief Removes the trigger edge between the two defined
 *        nodes in the DIG
 *        NOTE: This only removes from the application representation
 *              and requires a sim_user_pf_set_param() call to
 *              push the changes to the simulator
 * @param baseaddr_from Base addr of the from node
 * @param baseaddr_to Base addr of the to node
 * @retval Int 0 on success
 */
int
pf_delete_trig(uintptr_t baseaddr_from, uintptr_t baseaddr_to)
{
	params->DeleteTrigEdge(baseaddr_from, baseaddr_to);
	return 0;
}

/**
 * @brief Removes the all trigger edges from DIG
 *        NOTE: This only removes from the application representation
 *              and requires a sim_user_pf_set_param() call to
 *              push the changes to the simulator
 * @retval Int 0 on success
 */
int
pf_clear_trig()
{
	params->ClearTrigEdges();
	return 0;
}

int sim_user_wait()
{
	int err = 0;

	if (SimInSimulator() and !enable->is_enabled()) {
		enable->wait();
	}

	return err;
}

int sim_roi_start()
{
	SimRoiStart();

    printf("gapbs: bfs_enable_t @ %p\n", &enable); // don't ask why: absolutely need this print here
                                                 // to pass correct address

    // Can this be implicitly called by SimRoiStart? - Yes
    SimUser(PF_ENABLE, (long unsigned int) &enable);

    // And this as well? - Yes
    if (SimInSimulator() and !enable->is_enabled()) {
    	printf("%s %s %d: waiting\n", __FILE__, __FUNCTION__, __LINE__);
        enable->wait();
    }
	return 0;
}

int sim_roi_end()
{
	SimRoiEnd();
	SimUser(PF_DISABLE,0);
	return 0;
}

int sim_user_pf_disable()
{
	SimUser(PF_DISABLE,0);
	return 0;
}

int delete_params()
{
	delete params;
	return 0;
}

int delete_enable()
{
	delete enable;
	return 0;
}

} // extern "C"

#endif /* INCLUDE_PF_C_INTERFACE_H_ */
