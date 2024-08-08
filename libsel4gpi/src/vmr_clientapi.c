/**
 * @file vmr_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the vmr client API from vmr_clientapi.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <vka/vka.h>
#include <vka/capops.h>

#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/gpi_rpc.h>

// Defined for utility printing macros
#define DEBUG_ID ADS_DEBUG
#define SERVER_ID ADSSERVC
#define DEFAULT_ERR VmrComponentError_UNKNOWN

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &AdsMessage_msg,
    .reply_desc = &AdsReturnMessage_msg,
};

int vmr_client_reserve(seL4_CPtr ep,
                       void *vaddr,
                       size_t size,
                       size_t page_bits,
                       sel4utils_reservation_type_t vmr_type,
                       ads_vmr_context_t *ret_conn,
                       void **ret_vaddr)
{
    OSDB_PRINTF("Sending reserve request to VMR component\n");

    int error = 0;

    AdsMessage msg = {
        .magic = VMR_RPC_MAGIC,
        .which_msg = AdsMessage_reserve_tag,
        .msg.reserve = {
            .vaddr = (uint64_t)vaddr,
            .type = vmr_type,
            .size = size,
            .page_bits = page_bits,
        }};

    AdsReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        *ret_vaddr = (void *)ret_msg.msg.reserve.vaddr;
        ret_conn->ep = ret_msg.msg.reserve.slot;
    }

    return error;
}

int vmr_client_attach(ads_vmr_context_t *reservation,
                      mo_client_context_t *mo,
                      size_t offset)
{
    OSDB_PRINTF("Sending attach request to VMR component\n");

    int error = 0;

    AdsMessage msg = {
        .magic = VMR_RPC_MAGIC,
        .which_msg = AdsMessage_attach_tag,
        .msg.attach = {
            .offset = offset,
        }};

    AdsReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, reservation->ep, (void *)&msg,
                             1, &mo->ep, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int vmr_client_attach_no_reserve(seL4_CPtr vmr_rde,
                                 void *vaddr,
                                 mo_client_context_t *mo_cap,
                                 sel4utils_reservation_type_t vmr_type,
                                 void **ret_vaddr)
{
    OSDB_PRINTF("Sending attach-no-reserve request to VMR component\n");

    int error = 0;

    AdsMessage msg = {
        .magic = VMR_RPC_MAGIC,
        .which_msg = AdsMessage_attach_no_reserve_tag,
        .msg.attach_no_reserve = {
            .vaddr = (uint64_t)vaddr,
            .type = vmr_type,
        }};

    AdsReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, vmr_rde, (void *)&msg,
                             1, &mo_cap->ep, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        *ret_vaddr = (void *)ret_msg.msg.attach_no_reserve.vaddr;
    }

    return error;
}

int vmr_client_delete(ads_vmr_context_t *reservation)
{
    OSDB_PRINTF("Sending delete request to VMR component\n");

    int error = 0;

    AdsMessage msg = {
        .magic = VMR_RPC_MAGIC,
        .which_msg = AdsMessage_delete_tag};

    AdsReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, reservation->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int vmr_client_delete_by_vaddr(seL4_CPtr vmr_rde, void *vaddr)
{
    OSDB_PRINTF("Sending delete-by-vaddr request to VMR component\n");

    int error = 0;

    AdsMessage msg = {
        .magic = VMR_RPC_MAGIC,
        .which_msg = AdsMessage_delete_by_vaddr_tag,
        .msg.delete_by_vaddr = {
            .vaddr = (uint64_t)vaddr,
        }};

    AdsReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, vmr_rde, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int vmr_client_disconnect(ads_vmr_context_t *reservation)
{
    OSDB_PRINTF("Sending delete request to VMR component\n");

    int error = 0;

    AdsMessage msg = {
        .magic = VMR_RPC_MAGIC,
        .which_msg = AdsMessage_delete_tag};

    AdsReturnMessage ret_msg = {0};

    error = sel4gpi_rpc_call(&rpc_env, reservation->ep, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}