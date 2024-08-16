
/**
 * @file API for a client to communicate with the sample server
 */

#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/mo_clientapi.h>
#include <sample_shared.h>

/**
 * Structure maintained on the client side for every resource from the sample server
 */
typedef struct _sample_client_context
{
    seL4_CPtr ep;

    /* INSERT HERE more fields, if needed */
} sample_client_context_t;

/**
 * Starts the sample server in a new process
 *
 * @param sample_server_pd returns the PD resource of the new sample server
 * @param sample_space_id returns the resource space ID of the sample server
 * @return 0 on success, or -1 otherwise
 */
int start_sample_server_proc(pd_client_context_t *sample_server_pd, gpi_space_id_t *sample_space_id);

/**
 * @brief Allocate a new resource from the sample server
 *
 * Requests a new resource from the sample server, returning
 * a connection object for the new resource on success.
 *
 * @param server_ep Well known server endpoint cap.
 * @param ret_conn resource's connection object
 * @return int 0 on success, error code otherwise
 */
int sample_client_alloc(seL4_CPtr server_ep, sample_client_context_t *ret_conn);

/**
 * @brief Release a resource to the sample server
 *
 * @param conn connection for the resource to free
 * @return int 0 on success, error code otherwise
 */
int sample_client_free(sample_client_context_t *conn);

/**
 * @brief Invoke a resource from the sample server
 *
 * @param conn connection for the resource to invoke
 * @param x some argument for the operation
 * @param y some argument for the operation
 * @param response the response string will be written here, to a maximum of 40 characters
 * @return int 0 on success, error code otherwise
 */
int sample_client_invoke(sample_client_context_t *conn, uint64_t x, uint64_t y, char *response);

/* INSERT HERE more client api functions */