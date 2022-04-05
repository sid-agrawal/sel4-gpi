#pragma once

#include <sys/types.h>
#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

typedef struct _counter_client_context {
   cspacepath_t badged_server_ep_cspath;
   cspacepath_t public_server_ep_cspath;
} counter_client_context_t;

/**
 * @brief   Initialize the counter client.
 * 
 * @param server_ep_cap Well known server endpoint cap.
 * @param client_vka client's cka for allocating memory.
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int counter_server_client_connect(seL4_CPtr server_ep_cap,
                                  vka_t *client_vka,
                                  counter_client_context_t *ret_conn);       


/**
 * @brief   Disconnect the counter client.
 * 
 * @param ret_conn 
 * @return int 0 on success, -1 on failure.
 */
int counter_server_client_disconnect(counter_client_context_t *ret_conn);

/**
 * @brief   Increment the counter.
 * 
 * @param conn 
 * @return int 0 on success, -1 on failure.
 */
int counter_client_increment(counter_client_context_t *conn);

/**
 * @brief   Decrement the counter.
 * 
 * @param conn 
 * @return int 0 on success, -1 on failure.
 */
int counter_client_decrement(counter_client_context_t *conn);