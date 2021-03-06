#pragma once

#include <sys/types.h>
#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/cpu_component.h>
#include <sel4gpi/ads_clientapi.h>

typedef struct _cpu_client_context {
   cspacepath_t badged_server_ep_cspath;
   //cspacepath_t public_server_ep_cspath;
} cpu_client_context_t;

/**
 * @brief   Initialize the cpu client.
 * 
 * @param server_ep_cap Well known server endpoint cap.
 * @param client_vka client's cka for allocating memory.
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int cpu_component_client_connect(seL4_CPtr server_ep_cap,
                              vka_t *client_vka,
                              cpu_client_context_t *ret_conn);       


/**
 * @brief   Disconnect the cpu client.
 * 
 * @param conn 
 * @return int 0 on success, -1 on failure.
 */
int cpu_component_client_disconnect(cpu_client_context_t *conn);

/**
 * @brief 
 * 
 * @param conn client connection object
 * @param entry_fn the address of the function to be called when the thread starts.
 * @return int 0 on success, -1 on failure.
 */
int cpu_client_start(cpu_client_context_t *conn,
                     sel4utils_thread_entry_fn entry_fn);

/**
 * @brief Configure the cpu oject.
 * 
 * @param conn client connection object
 * @param ads_conn ads connection object
 * @param cspace_root cspace root for the cpu object.
 * @return int 0 on success, -1 on failure.
 */
int cpu_client_config(cpu_client_context_t *conn,
                      ads_client_context_t *ads_conn,
                      seL4_CPtr cspace_root);