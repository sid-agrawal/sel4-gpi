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

/**
 * @brief   Initialize the pd client.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param free_slot a slot to receive a cap in
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int pd_component_client_connect(seL4_CPtr server_ep_cap,
                                seL4_CPtr free_slot,
                                pd_client_context_t *ret_conn);

/**
 * @brief   Disconnect the pd client.
 * Destroys all internal metadata associated with the PD.
 * 
 * (XXX) Arya: Should this immediately delete the PD, or just decrement refcount?
 * (XXX) Arya: Should there be options to destroy resources
 * held by the PD?
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int pd_client_disconnect(pd_client_context_t *conn);

/**
 * @brief Send a cap to PD and gets the slot number in the PD.
 *
 * @param conn client connection object
 * @param cap_to_send cap to send to the PD
 * @param slot slot in the PD where the cap was installed
 * @return int 0 on success, -1 on failure.
 */
int pd_client_send_cap(pd_client_context_t *conn, seL4_CPtr cap_to_send,
                       seL4_Word *slot);

/**
 * @brief Get the next free slot
 *
 * @param conn client connection object
 * @param slot next free slot in the PD
 * @return int 0 on success, -1 on failure.
 */
int pd_client_next_slot(pd_client_context_t *conn, seL4_Word *slot);

/**
 * @brief Free an unused slot in the PD
 *
 * @param conn client connection object
 * @param slot slot to free in the PD
 * @return int 0 on success, -1 on failure.
 */
int pd_client_free_slot(pd_client_context_t *conn,
                        seL4_CPtr slot);
/**
 * @brief Create a badged copy of an endpoint capability
 *
 * @param conn client connection object
 * @param ret_ep location of result endpoint
 * @return int 0 on success, -1 on failure.
 */
int pd_client_alloc_ep(pd_client_context_t *conn,
                       seL4_CPtr *ret_ep);

/**
 * @brief Create a badged copy of an endpoint capability
 * (XXX) Arya: TO BE DEPRECATED
 * @param conn client connection object
 * @param src_ep raw endpoint in pd's cspace
 * @param badge badge to apply to the endpoint
 * @param ret_ep location of result endpoint
 * @return int 0 on success, -1 on failure.
 */
int pd_client_badge_ep(pd_client_context_t *conn,
                       seL4_CPtr src_ep,
                       seL4_Word badge,
                       seL4_CPtr *ret_ep);

/**
 * @brief Dump the PD.
 * @param conn client connection object
 * @param buf buffer to dump the PD into
 * @param size size of the buffer
 * @return int 0 on success, -1 on failure.
 */
int pd_client_dump(pd_client_context_t *conn,
                   char *buf, size_t size);

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
                        uint64_t space_id);

/* -- Resource Manager Functions -- */
// (XXX) Arya: In the process of moving these to a separate component

/**
 * To be called by a resource server when it allocates
 * a resource to another PD
 *
 * @param conn the resource server's pd connection
 * @param res_space_id the resource space ID
 * @param recipient_id the recipient PD's ID
 * @param resource_id unique ID of the resource within the resource space
 * @param dest returns the destination slot in the recipient PD
 */
int pd_client_give_resource(pd_client_context_t *conn,
                            seL4_Word res_space_id,
                            seL4_Word recipient_id,
                            seL4_Word resource_id,
                            seL4_CPtr *dest);

/**
 * Called by a PD to notify that it is about to exit
 * This call has no reply
 * 
 * @param conn the connection of the PD that is exiting
*/
void pd_client_exit(pd_client_context_t *conn);

void pd_client_bench_ipc(pd_client_context_t *conn, seL4_CPtr dummy_send_cap, seL4_CPtr dummy_recv_cap, bool cap_transfer);

/**
 * @brief (WIP) prepares the (PD, ADS, CPU) combination with the given arguments, entry point, stack, and IPC buffer
 * TODO Linh: better explain what differs between setup types
 *
 * @param target_ads the ADS where the stack resides
 * @param target_pd the process PD which will use this stack
 * @param target_cpu the CPU which will execute in this ADS and PD
 * @param stack_pos pointer to a position in the stack, depends on the setup type
 * @param stack_size size of the stack (in pages)
 * @param argc the number of arguments to place on the stack
 * @param args the arguments
 * @param entry_point the address of the instruction to start executing at (in the target ADS)
 * @param ipc_buf_addr the address of the IPC buffer for the (PD, ADS, CPU) combination
 * @param setup_type the type of setup (see pd_setup_type_t for details)
 * @return int 0 on success
 */
int pd_client_runtime_setup(pd_client_context_t *target_pd,
                            ads_client_context_t *target_ads,
                            cpu_client_context_t *target_cpu,
                            void *stack_pos,
                            int stack_size,
                            int argc,
                            seL4_Word *args,
                            void *entry_point,
                            void *ipc_buf_addr,
                            pd_setup_type_t setup_type);

/**
 * @brief (WIP)
 *
 * @param src_pd
 * @param dest_pd
 * @param res_type
 * @return int
 */
int pd_client_share_resource_by_type(pd_client_context_t *src_pd, pd_client_context_t *dest_pd, gpi_cap_t res_type);
