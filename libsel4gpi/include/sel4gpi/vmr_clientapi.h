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
 * Reserve a VMR of an ADS
 *
 * @param ep the VMR RDE endpoint2
 * @param vaddr requested reservation address (or NULL)
 * @param size size in bytes of the region to reserve
 * @param page_bits size of an individual page
 * @param vmr_type the type of virtual memory (e.g. stack, heap, ipc buffer)
 * @param[out] ret_conn returns the context for the reserved VMR
 * @param[out] ret_vaddr return virtual address of the reservation
 * @return int 0 on success, 1 on failure
 */
int vmr_client_reserve(seL4_CPtr ep,
                       void *vaddr,
                       size_t size,
                       size_t page_bits,
                       sel4utils_reservation_type_t vmr_type,
                       ads_vmr_context_t *ret_conn,
                       void **ret_vaddr);

/**
 * Attach an MO to a VMR reservation
 *
 * @param reservation reservation to attach to
 * @param mo mo to attach
 * @param offset offset into the reservation to attach the MO
 * @return int 0 on success, 1 on failure.
 */
int vmr_client_attach(ads_vmr_context_t *reservation,
                      mo_client_context_t *mo,
                      size_t offset);

/**
 * Simultaneously reserve a VMR and attach an MO to it.
 * The reservation will bo of the correct size to attach the MO to.
 *
 * @param vmr_rde the endpoint for the VMR space
 * @param vaddr virtual address to attach at, can be NULL
 * @param mo_cap MO cap of the memory to attach
 * @param vmr_type the type of virtual memory (e.g. stack, heap, ipc buffer)
 * @param ret_vaddr virtual address where the MO was attached.
 * @return int 0 on success, 1 on failure.
 */
int vmr_client_attach_no_reserve(seL4_CPtr vmr_rde,
                                 void *vaddr,
                                 mo_client_context_t *mo_cap,
                                 sel4utils_reservation_type_t vmr_type,
                                 void **ret_vaddr);

/**
 * @brief
 * Delete a VMR by removing it from the parent ADS.
 *
 * @param conn the VMR reservation
 * @return int 0 on success, 1 on failure.
 */
int vmr_client_delete(ads_vmr_context_t *reservation);

/**
 * @brief
 * Delete a VMR by removing it from the parent ADS.
 * This takes a vaddr instead of a reservation, to be used with vmr_client_attach_no_reserve.
 * Removes the entire reservation starting at the provided vaddr.
 *
 * @param vmr_rde the endpoint for the VMR space
 * @param vaddr virtual address to remove
 * @return int 0 on success, 1 on failure.
 */
int vmr_client_delete_by_vaddr(seL4_CPtr vmr_rde, void *vaddr);

/**
 * @brief
 * Remove the VMR resource from this PD.
 * If this was the last copy, the VMR is also deleted.
 *
 * @param conn the VMR reservation
 * @return int 0 on success, 1 on failure.
 */
int vmr_client_disconnect(ads_vmr_context_t *reservation);