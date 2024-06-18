/**
 * @file endpoint_clientapi.h
 * @author Linh Pham (phamhlinh01@gmail.com)
 * @brief API calls to allocate tracked endpoints
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

/** The client context for an endpoint - this is a bit weird, since there's 2 endpoints wrapped in this:
 *  1) badged_server_ep_cspath: the endpoint to the EP component, which allocates new **tracked** endpoints
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
    cspacepath_t badged_server_ep_cspath; ///< badged endpoint to the EP component in the RT,
                                          ///< allows for resource operations on the tracked endpoint
    seL4_CPtr raw_endpoint;               ///< the underlying endpoint, it may not always be filled in (e.g. in the case
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
 * @brief retrieves the raw, underlying endpoint of an endpoint context, and fills it in the given context
 * This is used when a PD is sent a tracked endpoint, it will receive only the badged version that allows
 * communication with the EP component. In order to listen on the actual endpoint, it needs to retrieve it via
 * this API call.
 *
 * @param ep_conn the endpoint context
 * @return int 0 on success, 1 on failure
 */
int ep_client_get_raw_endpoint(ep_client_context_t *ep_conn);
