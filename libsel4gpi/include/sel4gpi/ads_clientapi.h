#pragma once

#include <sys/types.h>
#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_component.h>
#include <sel4gpi/gpi_client.h>

/**
 * @brief   Initialize the ads client.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param free_slot a slot to receive a cap in
 * @param ret_conn client's connection object
 * @param ret_ads_ns the NS ID of the ADS resource, for access to its RDE, can be NULL
 * @return int 0 on success, -1 on failure.
 */
int ads_component_client_connect(seL4_CPtr server_ep_cap,
                                 seL4_CPtr free_slot,
                                 ads_client_context_t *ret_conn,
                                 seL4_Word *ret_ads_ns);

/**
 * @brief   Disconnect the ads client.
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int ads_component_client_disconnect(ads_client_context_t *conn);

/**
 * @brief
 *
 * @param conn client connection object
 * @param vaddr virtual address to attach at, can be NULL
 * @param mo_cap MO cap of the memory to attach
 * @param vmr_type the type of virtual memory (e.g. stack, heap, ipc buffer)
 * @param ret_vaddr virtual address where the MO was attached.
 * @return int 0 on success, -1 on failure.
 */
int ads_client_attach(ads_client_context_t *conn,
                      void *vaddr,
                      mo_client_context_t *mo_cap,
                      sel4utils_reservation_type_t vmr_type,
                      void **ret_vaddr);

/**
 * @brief   
 * Remove a memory region from the ads.
 * Removes the entire reservation starting at the provided vaddr.
 * (XXX) Arya: This operation is really VMR delete
 *
 * @param conn client connection object
 * @param vaddr virtual address to remove
 * @return int 0 on success, -1 on failure.
 */
int ads_client_rm(ads_client_context_t *conn, void *vaddr);

/**
 * @brief Attach a given ads to to a given CPU cap.
 *
 * @param conn client connection object
 * @param cpu_cap CAP of the CPU to bind to. For now the CPU cap is just the TCB cap.
 * @return int 0 on success, -1 on failure.
 */
int ads_client_bind_cpu(ads_client_context_t *conn, seL4_CPtr cpu_cap);

/**
 * @brief Shallow Copy the ads cap, that is make a new ADS cap that is a copy of the original.
 * @param conn original ads connection object
 * @param omit_vaddr Do not shallow copy the segment with this starting VA
 * @param ads_cap_ret return cap
 * @return int 0 on success, -1 on failure.
 */
int ads_client_shallow_copy(ads_client_context_t *conn, seL4_CPtr free_slot, void *omit_vaddr,
                            ads_client_context_t *conn_ret);

/**
 * @brief Dump the resource relations of the ads.
 * @param conn ads connection object
 * @return int 0 on success, -1 on failure.
 */
int ads_client_dump_rr(ads_client_context_t *conn, char *buf, size_t buf_size);

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

/**
 * @brief Load an image's ELF into the given ADS
 *
 * @param loadee_ads
 * @param image_name
 * @return int
 */
int ads_client_load_elf(ads_client_context_t *loadee_ads, const char *image_name);
