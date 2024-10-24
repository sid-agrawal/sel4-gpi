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
#include <sel4gpi/pd_creation.h>

/**
 * @brief   Initialize the ads client.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param ret_conn returns the client connection object
 * @return int 0 on success, 1 on failure.
 */
int ads_component_client_connect(seL4_CPtr server_ep_cap,
                                 ads_client_context_t *ret_conn);

/**
 * @brief   Disconnect the ads client.
 *
 * @param conn
 * @return int 0 on success, 1 on failure.
 */
int ads_client_disconnect(ads_client_context_t *conn);

/**
 * @brief Attach a given ads to to a given CPU cap.
 *
 * @param conn client connection object
 * @param cpu_cap CAP of the CPU to bind to. For now the CPU cap is just the TCB cap.
 * @return int 0 on success, 1 on failure.
 */
int ads_client_bind_cpu(ads_client_context_t *conn, seL4_CPtr cpu_cap);

/**
 * @brief Given a VMR config that describes a virtual memory region, shallow copies it from src_ads to dst_ads
 *
 * For regions with type other than SEL4UTILS_RES_TYPE_GENERIC and SEL4UTILS_RES_TYPE_SHARED_FRAMES,
 * every config option except `type` can be omitted, and the server will attempt to look for the typed VMR
 * to shallow copy.
 *
 * @param src_ads the ADS to copy from
 * @param dst_ads the ADS to copy to
 * @param vmr_vfg the config describing one VMR, the `share_mode` and `mo` option are ignored
 * @return int 0 on success
 */
int ads_client_shallow_copy(ads_client_context_t *src_ads,
                            ads_client_context_t *dst_ads,
                            vmr_config_t *vmr_cfg);

/**
 * @brief Obtains info about an ADS reservation for a given VMR type. If multiple reservations of
 * the same type exist, info about the first one found is returned.
 *
 * @param ads the ADS to search in
 * @param res_type the type of VMR to find the reservation for
 * @param[out] ret_vaddr the starting virtual address of the reservation, NULL if it wasn't found
 * @param[out] ret_num_pages OPTIONAL: the number of pages in the reservation, 0 if it wasn't found
 * @param[out] ret_page_bits OPTIONAL: the size bits of an individual page, 0 if it wasn't found
 * @return int 0 on success, other on error
 */
int ads_client_get_reservation(ads_client_context_t *ads, sel4utils_reservation_type_t res_type,
                               void **ret_vaddr, size_t *ret_num_pages, size_t *ret_page_bits);

/* ======================================= CONVENIENCE FUNCTIONS (NOT PART OF FRAMEWORK) ================================================= */

/**
 * @brief Load an image's ELF into the given ADS
 * NOTE: this is not part of the OSmosis framework, it is here for convenience
 *
 * @param loadee_ads the ADS to load the ELF into
 * @param loadee_pd the PD which is being set up with this ELF
 * @param image_name name of the ELF image to load
 * @param ret_entry_point the vaddr entry point of the loaded ELF
 * @return int 0 on success
 */
int ads_client_load_elf(ads_client_context_t *loadee_ads, pd_client_context_t *loadee_pd, const char *image_name, void **ret_entry_point);
