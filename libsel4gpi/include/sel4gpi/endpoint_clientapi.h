/**
 * @file endpoint_clientapi.h
 * @author Linh Pham (phamhlinh01@gmail.com)
 * @brief client facing functions for tracked endpoint operations
 * @version 0.1
 * @date 2024-06-18
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once
#include <stdint.h>
#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <sel4gpi/pd_client_context.h>

/** The client context for an endpoint - this is a bit weird, since there's 2 endpoints wrapped in this:
 *  1) ep: the endpoint to the EP component, which allocates new **tracked** endpoints
 *     This is the endpoint to supply to `pd_client_send` when the raw endpoint should be sent to other PDs
 *  2) raw_endpoint: the actual endpoint that can be listened on
 *
 *  This is different from other GPI resources, in that a PD can directly access the raw underlying resource.
 *  However, endpoints are not canonical OSmosis resources and they're implemented as GPI
 *  resources only for the purpose of being able to track and clean them up.
 *  A PD can theoretically do whatever it wants with the raw endpoint (e.g. directly use a syscall to delete it),
 *  but it shouldn't matter to the EP component, as that only cares about its conceptual existence
 */
typedef struct _ep_client_context
{
    seL4_CPtr ep;           ///< badged endpoint to the EP component in the RT,
                            ///< allows for resource operations on the tracked endpoint
    seL4_CPtr raw_endpoint; ///< the underlying endpoint, it may not always be filled in (e.g. in the case
                            ///< that this was received from another PD), and may need to be retrieved via
                            ///< ep_client_get_raw_endpoint)
} ep_client_context_t;

/**
 * @brief Allocate a new, unbadged, tracked endpoint
 *
 * @param server_ep_cap RDE to the EP component
 * @param[out] ret_conn EP connection object
 * @return int 0 on success, 1 on failure.
 */
int ep_component_client_connect(seL4_CPtr server_ep_cap, ep_client_context_t *ret_conn);

/**
 * @brief Remove an endpoint resource from this PD
 *
 * @param conn EP connection object
 * @return int 0 on success, 1 on failure.
 */
int ep_component_client_disconnect(ep_client_context_t *conn);

/**
 * @brief Retrieves the raw, underlying endpoint of an endpoint context in the CSpace of the given target PD.
 *
 * @param target_PD the target PD to get the EP slot from
 * @param ep_conn the EP context held by the **current** PD (NOT the target)
 * @param[out] ret_ep returns the slot of the raw EP in target PD
 * @return int 0 on success, 1 on failure.
 */
int ep_client_get_raw_endpoint_in_PD(pd_client_context_t *target_PD, ep_client_context_t *ep_conn, seL4_CPtr *ret_ep);

/**
 * @brief retrieves the raw, underlying endpoint of an endpoint context for the current PD,
 * and fills it in the given context. This is used when a PD is sent a tracked endpoint,
 * it will receive only the badged version that allows communication with the EP component.
 * In order to listen on the actual endpoint, it needs to retrieve it via this API call.
 *
 * @param ep_conn the endpoint context
 * @return int 0 on success, 1 on failure
 */
int ep_client_get_raw_endpoint(ep_client_context_t *ep_conn);

/**
 * @brief Forges a tracked endpoint from an existing endpoint. This is only meant to be called by the
 * test PDs, since their endpoints were already set up by sel4test.
 *
 * @param server_ep_cap RDE to EP component
 * @param ret_conn[out] EP connection object
 * @return int 0 on success, 1 on failure.
 */
int ep_client_forge(seL4_CPtr server_ep_cap, seL4_CPtr ep_to_forge, ep_client_context_t *ret_conn);
