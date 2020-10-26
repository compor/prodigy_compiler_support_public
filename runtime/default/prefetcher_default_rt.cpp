/*
 * pf_c_interface.h
 *
 *  Created on: 7 Nov 2019
 *      Author: kuba
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
	params = new pf_params_t(num_nodes_pf, num_edges_pf, num_triggers_pf, 4);
	printf("pf: &params = %p\n", params);

	return params_id;
}

int create_enable()
{
	int enable_id = 0;
	enable = new pf_enable_t();
	printf("pf: &enable = %p\n", enable);

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

int
register_trav_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f)
{
	int err = 0;

	(void) params->RegisterTravEdge(baseaddr_from, baseaddr_to, f);

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
