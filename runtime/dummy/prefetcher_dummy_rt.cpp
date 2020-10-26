//
//
//

#include <stdio.h>
// using fprintf

#include <stdint.h>
// using uintptr_t

#include "pf_interface.h"

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

} // extern "C"

int create_params(int num_nodes_pf, int num_edges_pf, int num_triggers_pf) {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int create_enable() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int register_node(void *base, int64_t size, int64_t node_id) {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int register_node_with_size(uintptr_t base, int64_t size, int64_t elem_size,
                            int64_t node_id) {
  fprintf(stderr, "calling func: %s %lx %p\n", __func__, (uint64_t)base, base);
  return 0;
}

int register_trav_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to,
                       FuncId f) {
  fprintf(stderr, "calling func: %s %lx %lx\n", __func__, baseaddr_from, baseaddr_to);
  return 0;
}

int register_trav_edge2(NodeId id_from, NodeId id_to, FuncId f) {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int register_trig_edge1(uintptr_t baseaddr_from, uintptr_t baseaddr_to, FuncId f,
                       FuncId sq_f) {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int register_trig_edge2(NodeId id_from, NodeId id_to, FuncId f, FuncId sq_f) {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int sim_user_pf_set_param() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int sim_user_pf_set_enable() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int sim_user_pf_enable() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int sim_user_wait() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int sim_roi_start() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int sim_roi_end() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int sim_user_pf_disable() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int delete_params() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int delete_enable() {
  fprintf(stderr, "calling func: %s\n", __func__);
  return 0;
}

int register_identify_edge_source(uintptr_t baseaddr_from, int edge_id)
{
	int err = 0;

	fprintf(stderr, "Edge source activation %d, edge %d\n", baseaddr_from, edge_id);

	return err;
}

int register_identify_edge_target(uintptr_t baseaddr_to, int edge_id)
{
	fprintf(stderr, "Edge target activation %d, edge %d\n", baseaddr_to, edge_id);
	return 0;
}


