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

typedef struct _pd_client_context {
   cspacepath_t badged_server_ep_cspath;
   //cspacepath_t public_server_ep_cspath;
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
int pd_client_load(pd_client_context_t *conn,
                   const char *image);

/**
 * @brief Start the pd oject.
 * 
 * @param conn client connection object
 * @return int 0 on success, -1 on failure.
 */
int pd_client_start(pd_client_context_t *conn);