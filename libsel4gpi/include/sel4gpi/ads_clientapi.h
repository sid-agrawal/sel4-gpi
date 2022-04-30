#pragma once

#include <sys/types.h>
#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_server.h>

typedef struct _ads_client_context {
   cspacepath_t badged_server_ep_cspath;
   //cspacepath_t public_server_ep_cspath;
} ads_client_context_t;

/**
 * @brief   Initialize the ads client.
 * 
 * @param server_ep_cap Well known server endpoint cap.
 * @param client_vka client's cka for allocating memory.
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int ads_server_client_connect(seL4_CPtr server_ep_cap,
                                  vka_t *client_vka,
                                  ads_client_context_t *ret_conn);       


/**
 * @brief   Disconnect the ads client.
 * 
 * @param conn 
 * @return int 0 on success, -1 on failure.
 */
int ads_server_client_disconnect(ads_client_context_t *conn);

/**
 * @brief 
 * 
 * @param conn client connection object
 * @param vaddr virtual address to attach at
 * @param size size of the attached region
 * @param frame_cap frame cap of the memory to attach(this will be changed to a MR cap (TODO(siagraw)))
 * @return int 0 on success, -1 on failure.
 */
int ads_client_attach(ads_client_context_t *conn, void* vaddr, size_t size, seL4_CPtr frame_cap);

/**
 * @brief   Remove a memory region from the ads.
 * 
 * @param conn client connection object
 * @param vaddr virtual address to attach at
 * @param size size of the attached region
 * @return int 0 on success, -1 on failure.
 */
int ads_client_rm(ads_client_context_t *conn, void* vaddr, size_t size);

/**
 * @brief Attach a given ads to to a given CPU cap.
 * 
 * @param conn client connection object
 * @param cpu_cap CAP of the CPU to bind to. For now the CPU cap is just the TCB cap.
 * @return int 0 on success, -1 on failure.
 */
int ads_client_bind_cpu(ads_client_context_t *conn, seL4_CPtr cpu_cap);


/**
 * @brief Clone the ads cap, that is make a new ADS cap that is a copy of the original.
 * @param conn original ads connection object
 * @param omit_vaddr Do not clone the segment with this starting VA
 * @param ads_cap_ret return cap
 * @return int 0 on success, -1 on failure.
 */
int ads_client_clone(ads_client_context_t *conn, vka_t *vka, void* omit_vaddr,    
                     ads_client_context_t *conn_ret);

/**
 * @brief Get the unique id of the ads.
 * 
 * @param conn ads connection object
 * @param ret_id id of the ads as a return value
 * @return int 0 on success, -1 on failure.
 */
int ads_client_getID(ads_client_context_t *conn, seL4_Word *ret_id);

int ads_client_testing(ads_client_context_t *conn, vka_t *vka,
                       ads_client_context_t *ads_conn_clone1,
                       ads_client_context_t *ads_conn_clone2,
                       ads_client_context_t *ads_conn_clone3);