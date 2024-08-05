#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_obj.h>
#include <sel4gpi/resource_component_utils.h>
#include <ads_component_rpc.pb.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define ADS_RPC_MAGIC 0x414453
#define ADSSERVS "ADSServ Component: "
#define ADSSERVC "ADSServ Client   : "

/* Per-client context maintained by the server. */
typedef struct _ads_component_registry_entry
{
    resource_registry_node_t gen;
    ads_t ads;
} ads_component_registry_entry_t;

/**
 * To initialize the ads component at the beginning of execution
 */
int ads_component_initialize(vka_t *server_vka,
                             vspace_t *server_vspace,
                             vka_object_t server_ep_obj);

/* Global server instance accessor functions. */
resource_component_context_t *get_ads_component(void);

/**
 * @brief Given a vspace_t insert it into the ADS server's metadata and return a cap to it.
 * This should only be used for the root task
 *
 * @param vspace The vspace to insert.
 * @param vka The vka instance to use for allocating the cap.
 * @param cap_ret The cap to the vspace.
 * @return int
 */
int forge_ads_cap_from_vspace(vspace_t *vspace, vka_t *vka, gpi_obj_id_t client_pd_id,
                              seL4_CPtr *cap_ret, gpi_obj_id_t *id_ret);

/**
 * Attach an MO to an ADS by ID
 * Note: Only useable from the root task
 *       This is needed since the root task cannot send IPCs to itself
 *
 * @param ads_id ID of the ADS to attach to
 * @param mo_id ID of the MO to attach
 * @param vmr_type type of VMR to attach to
 * @param vaddr Requested vaddr to attach at, or NULL
 * @param ret_vaddr Returns the attached vaddr
 */
int ads_component_attach(gpi_obj_id_t ads_id, gpi_obj_id_t mo_id,
                         sel4utils_reservation_type_t vmr_type, void *vaddr, void **ret_vaddr);

/**
 * Remove an MO from an ADS by vaddr
 * Note: Only useable from the root task
 *
 * @param ads_id ID of the ADS to remove from
 * @param vmr_id ID of the region to remove
 */
int ads_component_rm_by_id(gpi_obj_id_t ads_id, gpi_obj_id_t vmr_id);

/**
 * Remove an MO from an ADS by vaddr
 * Note: Only useable from the root task
 *
 * @param ads_id ID of the ADS to remove from
 * @param vaddr vaddr of the region to remove
 */
int ads_component_rm_by_vaddr(gpi_obj_id_t ads_id, void *vaddr);

/**
 * Map an MO to the root task's address space
 *
 * @param mo_id ID of the MO to attach
 * @param ret_vaddr Returns the attached vaddr
 */
int ads_component_attach_to_rt(gpi_obj_id_t mo_id, void **ret_vaddr);

/**
 * Unmap an MO from the root task's address space
 *
 * @param vaddr vaddr where the MO was attached
 */
int ads_component_remove_from_rt(void *vaddr);

/**
 * Allocate an ADS from the root task
 *
 * @param client_id the PD id of the client requesting the ADS
 * @param ret_ads returns the created ADS
 * @param ret_cap returns the slot of the new ADS, in the client (or NULL, to make no cap)
 */
int ads_component_allocate(gpi_obj_id_t client_id, ads_t **ret_ads, seL4_CPtr *ret_cap);

/**
 * Load an ELF image into an ADS
 *
 * @param ads the ADS to load the ELF into
 * @param target_pd the target PD that will be using this ADS
 *  (XXX) Arya: we may be able to remove this parameter
 * @param image_name the name of the image to load, must be in the cpio archive
 * @param entry_point returns the vaddr of the entry point in the ADS
 */
int ads_component_load_elf(ads_t *ads, pd_t *target_pd, char *image_name, void **entry_point);