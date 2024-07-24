#pragma once

#include <sys/types.h>
#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/pd_component.h>
#include <sel4gpi/pd_creation.h>
#include <pd_component_rpc.pb.h>

/**
 * @file Definition of the client API for interacting with the PD component
 */

#define PD_MAX_ARGC 8 // Should be the same as the length of the args array in the protobuf file

/** PD CREATION / INITIALIZATION **/

/**
 * @brief Create a new PD
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param osm_data_mo an MO for holding the PD's OSmosis data
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int pd_component_client_connect(seL4_CPtr server_ep_cap,
                                mo_client_context_t *osm_data_mo,
                                pd_client_context_t *ret_conn);

/**
 * @brief   Disconnect the pd client.
 * Destroys all internal metadata associated with the PD.
 * Kills the PD and executes the default cleanup policy.
 *
 * (XXX) Arya: Should this immediately delete the PD, or just decrement refcount?
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int pd_client_terminate(pd_client_context_t *conn);

/**
 * @brief Send a cap to PD and gets the slot number in the PD.
 *
 * @param conn client connection object
 * @param cap_to_send cap to send to the PD
 * @param slot OPTIONAL slot in the PD where the cap was installed
 * @return int 0 on success, 1 on failure.
 */
int pd_client_send_cap(pd_client_context_t *conn,
                       seL4_CPtr cap_to_send,
                       seL4_CPtr *slot);

/**
 * @brief Sends a core resource (PD, CPU, ADS) cap to a PD.
 * The sent cap will be set in the PD's OSmosis data frame,
 * overwriting any cap that was previously set.
 *
 * @param conn the PD to send the cap to
 * @param cap_to_send cap to send to the PD
 * @param slot OPTIONAL slot in the PD where the cap was installed
 * @return int 0 on success, 1 on failure.
 */
int pd_client_send_core_cap(pd_client_context_t *conn,
                            seL4_CPtr cap_to_send,
                            seL4_CPtr *slot);

/**
 * @brief Share an RDE with another PD
 * This shares an RDE from the client PD with the target PD (client PD is the PD which created the target PD)
 * The RDE is keyed by cap type and resource space ID
 *
 * @param target_pd the PD to share the RDE with
 * @param server_type resource type of the RDE to share
 * @param space_id resource space to share (optional, set to RESSPC_ID_NULL to share this PD's default space)
 * @return int 0 on success, -1 on failure.
 */
int pd_client_share_rde(pd_client_context_t *target_pd,
                        gpi_cap_t cap_type,
                        gpi_space_id_t space_id);

/**
 * @brief (WIP) prepares the (PD, ADS, CPU) combination with the given arguments,
 *        entry point, stack, and IPC buffer, and OSmosis data frame
 * This eventually will be removed in favour of a unified PD entry-point
 * TODO Linh: better explain what differs between setup types
 *
 * @param target_ads the ADS where the stack resides
 * @param target_pd the process PD which will use this stack
 * @param target_cpu the CPU which will execute in this ADS and PD
 * @param stack_pos pointer to a position in the stack, depends on the setup type
 * @param argc the number of arguments to place on the stack
 * @param args the arguments
 * @param entry_point the address of the instruction to start executing at (in the target ADS)
 * @param ipc_buf_addr the address of the IPC buffer for the (PD, ADS, CPU) combination
 * @param osm_data_in_PD address of the OSmosis data frame within the target ADS
 * @param setup_type the type of setup (see PdSetupType for details)
 * @return int 0 on success
 */
int pd_client_runtime_setup(pd_client_context_t *target_pd,
                            ads_client_context_t *target_ads,
                            cpu_client_context_t *target_cpu,
                            void *stack_pos,
                            int argc,
                            seL4_Word *args,
                            void *entry_point,
                            void *ipc_buf_addr,
                            void *osm_data_in_PD,
                            PdSetupType setup_type);

/**
 * @brief shares all resources of the given type from src_pd to dest_pd
 *
 * @param src_pd the source PD
 * @param dest_pd the destination PD
 * @param res_type the resource type to share
 * @return int 0 on success
 */
int pd_client_share_resource_by_type(pd_client_context_t *src_pd, pd_client_context_t *dest_pd, gpi_cap_t res_type);

/** CSPACE MANAGEMENT **/

/**
 * @brief Get the next free slot in the PD's cspace.
 *
 * @param conn client connection object
 * @param slot next free slot in the PD
 * @return int 0 on success, -1 on failure.
 */
int pd_client_next_slot(pd_client_context_t *conn, seL4_CPtr *slot);

/**
 * @brief Free an unused slot in the PD's cspace.
 * If the slot contains a capability, the capability will be deleted.
 * The slot will be marked free for future calls to pd_client_next_slot.
 *
 * @param conn client connection object
 * @param slot slot to free in the PD
 * @return int 0 on success, -1 on failure.
 */
int pd_client_free_slot(pd_client_context_t *conn,
                        seL4_CPtr slot);

/**
 * @brief Clear a slot in the PD's cspace
 * If the slot contains a capability, the capability will be deleted.
 * The slot will not be freed, and can be used again by the PD.
 *
 * @param conn client connection object
 * @param slot slot to free in the PD
 * @return int 0 on success, -1 on failure.
 */
int pd_client_clear_slot(pd_client_context_t *conn,
                         seL4_CPtr slot);

/** RESOURCE SERVER PD OPERATIONS **/

/**
 * To be called by a resource server when it allocates
 * a resource to another PD
 *
 * (XXX) Arya: Replace space/resource id with compact_res_id
 * @param conn the resource server's pd connection
 * @param res_space_id the resource space ID
 * @param recipient_id the recipient PD's ID
 * @param resource_id unique ID of the resource within the resource space
 * @param dest returns the destination slot in the recipient PD
 */
int pd_client_give_resource(pd_client_context_t *conn,
                            gpi_space_id_t res_space_id,
                            gpi_obj_id_t recipient_id,
                            gpi_obj_id_t resource_id,
                            seL4_CPtr *dest);

#if TRACK_MAP_RELATIONS
/**
 * To be called by a resource server when it maps a resource to another resource
 * The server must be the managing PD of the source resource's resource space
 * The source resource's resource space must map to the destination resource's space
 * (XXX) Arya: WIP
 *
 * @param conn the resource server's pd connection
 * @param src_res_id the universal ID of the source resource (compact_res_id)
 * @param dest_res_id the universal ID of the destination resource (compact_res_id)
 */
int pd_client_map_resource(pd_client_context_t *conn,
                           gpi_obj_id_t src_res_id,
                           gpi_obj_id_t dest_res_id);
#endif

/**
 * For a resource server to check if there is any work the root task needs it to do
 *
 * @param conn the resource server's pd connection
 * @param work this structure gets filled out with the return work
 * @return 0 on success, error otherwise
 */
int pd_client_get_work(pd_client_context_t *conn, PdWorkReturnMessage *work);

/**
 * @brief For a resource server to send a subgraph as a response to pd_client_get_work of type PdWorkAction_EXTRACT
 *
 * @param conn the resource server's pd connection
 * @param mo_conn an MO containing the model subgraph
 * @param has_data true if including an MO, false if there is no data to send
 * @param n_requests the number of requests that the PD is fulfilling
 * @return 0 on success, error otherwise
 */
int pd_client_send_subgraph(pd_client_context_t *conn, mo_client_context_t *mo_conn, bool has_data, int n_requests);

/**
 * @brief For a resource server to send as a response to pd_client_get_work of 
 * type PdWorkAction_FREE / PdWorkAction_DESTROY
 *
 * @param conn the resource server's pd connection
 * @param n_requests the number of requests that the PD is fulfilling
 * @return 0 on success, error otherwise
 */
int pd_client_finish_work(pd_client_context_t *conn, int n_requests);

/** OTHER FUNCTIONS FOR ACTIVE PDs **/

/**
 * Called by a PD to notify that it is about to exit
 * This call has no reply
 *
 * @param conn the connection of the PD that is exiting
 * @param code the exit code
 */
void pd_client_exit(pd_client_context_t *conn, int code);

/**
 * @brief Remove an RDE from a PD
 *
 * @param conn the target PD's connection object
 * @param type the type of RDE to remove
 * @param space_id the space ID of the RDE to remove, or RESSPC_ID_NULL to remove all RDEs of the given type
 * @return 0 on success, error otherwise
 */
int pd_client_remove_rde(pd_client_context_t *conn, gpi_cap_t type, gpi_space_id_t space_id);

/** MODEL EXTRACTION & BENCHMARKING **/

/**
 * @brief Dump the PD.
 * Note: This call may take a long time, since it may require subgraphs from multiple PDs
 * This should not be called by a resource server, since it may need to provide a subgraph
 *
 * @param conn client connection object
 * @param buf buffer to dump the PD into
 * @param size size of the buffer
 * @return int 0 on success, -1 on failure.
 */
int pd_client_dump(pd_client_context_t *conn,
                   char *buf, size_t size);

#ifdef CONFIG_DEBUG_BUILD
/**
 * @brief Assign a human-readable name to a PD, for debug / model extraction
 *
 * @param conn the target PD's connection object
 * @param name the name to assign
 * @return 0 on success, error otherwise
 */
int pd_client_set_name(pd_client_context_t *conn, char *name);
#endif

int pd_client_bench_ipc(pd_client_context_t *conn,
                        seL4_CPtr dummy_send_cap,
                        bool cap_transfer);