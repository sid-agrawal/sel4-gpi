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
 * @param client_vka client's cka for allocating memory.
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int pd_component_client_connect(seL4_CPtr server_ep_cap,
                                vka_t *client_vka,
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
 *
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
 * @param arg0 arg0 (XXX: Need to extend this to pass more args)
 * @return int 0 on success, -1 on failure.
 */
int pd_client_start(pd_client_context_t *conn, seL4_Word arg0);

/**
 * @brief add a new RDE to the PD
 *
 * @param conn client connection object
 * @param server_ep EP to the server
 * @param server_pd_cap PD cap of the server
 * @param server_type type of resource provided by server
 * @param needs_badge True if this is a raw endpoint that needs
 *                    to be badged with client PD's ID
 * @return int 0 on success, -1 on failure.
 */
int pd_client_add_rde(pd_client_context_t *conn,
                      seL4_CPtr server_ep,
                      seL4_CPtr server_pd_cap,
                      gpi_cap_t server_type,
                      bool needs_badge);

/**
 * To be called by a resource server when it starts running
 * This allows the GPI server to request resource relations
 * The resource server needs to prefix all of its resource IDs
 * with the given server_id, so resource IDs are globally unique
 *
 * @param conn the resource server's pd connection
 * @param server_ep the unbadged endpoint that the resource server listens on
 * @param server_id returns the resource server's unique ID
 */
int pd_client_register_resource_server(pd_client_context_t *conn,
                                       seL4_CPtr server_ep,
                                       seL4_Word *server_id);

/**
 * To be called by a resource server when it creates a new
 * resource and sends it to another PD
 *
 * @param conn the resource server's pd connection
 * @param recipient_id the recipient PD's ID
 * @param resource_type the type of the resource
 * @param resource_id unique ID of the resource
 */
int pd_client_give_resource(pd_client_context_t *conn,
                            seL4_Word recipient_id,
                            gpi_cap_t resource_type,
                            seL4_Word resource_id);