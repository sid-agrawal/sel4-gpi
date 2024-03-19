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
#include <sel4gpi/ads_clientapi.h>

// Default cap root and depth for pd cspace
#define PD_CAP_ROOT SEL4UTILS_CNODE_SLOT
#define PD_CAP_DEPTH seL4_WordBits

typedef struct _pd_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
} pd_client_context_t;

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
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int pd_component_client_disconnect(pd_client_context_t *conn);

/**
 * @brief
 *
 * @param conn client connection object
 * @param image elf image to load in the PD
 * @return int 0 on success, -1 on failure.
 */
int pd_client_load(pd_client_context_t *pd_os_cap,
                   ads_client_context_t *ads_os_cap,
                   const char *image);

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
 * @brief Start the pd oject.
 *
 * @param conn client connection object
 * @param argc number of args to pass to pd
 * @param args word args to pass to pd, length >= argc
 * @return int 0 on success, -1 on failure.
 */
int pd_client_start(pd_client_context_t *conn, int argc, seL4_Word *args);

/**
 * @brief Share an RDE with another PD
 * This shares an RDE from the client PD with the target PD
 * The RDE is keyed by cap type
 * Must call this AFTER the pd has been loaded
 * (XXX) Arya: Assumes only one RDE per cap type
 *
 * @param conn client connection object
 * @param server_type key of the RDE to share
 * @param ns_id namespace to share (optional, set to 0 for default namespace)
 * @return int 0 on success, -1 on failure.
 */
int pd_client_share_rde(pd_client_context_t *conn,
                        gpi_cap_t cap_type,
                        uint64_t ns_id);

/**
 * @brief Add a new RDE to the PD
 * This creates a new RDE for a resource manager in a freshly-started PD
 * There are up to 3 PDs involved in this operation:
 * - Client PD: Has created a resource server PD, calls this function
 * - Target PD: The PD to add an RDE in, the PD resource sent through conn
 * - Resource Manager PD: The PD that hosts a resource manager,
 *                        which the target PD's new RDE will point to
 *
 * @param conn client connection object
 * @param server_pd PD resource for the resource manager PD
 * @param manager_id Resource manager ID
 * @param ns_id Namespace ID, or NSID_DEFAULT
 * @return int 0 on success, -1 on failure.
 */
int pd_client_add_rde(pd_client_context_t *conn,
                      seL4_CPtr server_pd,
                      uint64_t manager_id,
                      uint64_t ns_id);

/* -- Resource Manager Functions -- */
// (XXX) Arya: Should these be part of a different component?

/**
 * To be called by a resource manager when it starts running
 * It will use the given manager_id to allocate resources in the future
 *
 * @param conn the resource server's pd connection
 * @param resource_type the resource type the manager provides
 * @param server_ep unbadged ep the resource server listens on
 * @param manager_id returns the resource manager's unique ID
 */
int pd_client_register_resource_manager(pd_client_context_t *conn,
                                        gpi_cap_t resource_type,
                                        seL4_CPtr server_ep,
                                        seL4_Word *manager_id);

/**
 * To be called by a resource manager to allocate a new namespace
 * It will use the given ns_id to refer to the ns in the future
 *
 * @param conn the resource server's pd connection
 * @param manager_id manager ID
 * @param ns_id returns the namespace ID
 */
int pd_client_register_namespace(pd_client_context_t *conn,
                                 seL4_Word manager_id,
                                 seL4_Word *ns_id);

/**
 * To be called by a resource manager when it creates a new resource
 *
 * @param conn the resource server's pd connection
 * @param manager_id the resource manager id, given by pd_client_register_resource_manager
 * @param resource_id id of the resource (local id to the resource manager)
 */
int pd_client_create_resource(pd_client_context_t *conn,
                              gpi_cap_t manager_id,
                              seL4_Word resource_id);

/**
 * To be called by a resource server when it allocates
 * a resource to another PD
 *
 * @param conn the resource server's pd connection
 * @param manager_id the resource manager id, given by pd_client_register_resource_manager
 * @param ns_id the namespace ID being allocated from, given by pd_client_register_namespace
 * @param recipient_id the recipient PD's ID
 * @param resource_id id of the resource (local id to the resource manager)
 * @param dest returns the destination slot in the recipient PD
 */
int pd_client_give_resource(pd_client_context_t *conn,
                            seL4_Word manager_id,
                            seL4_Word ns_id,
                            seL4_Word recipient_id,
                            seL4_Word resource_id,
                            seL4_CPtr *dest);