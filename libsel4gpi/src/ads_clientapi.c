/**
 * @file ads_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the ads client API from ads_client.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <vka/vka.h>
#include <vka/capops.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/gpi_rpc.h>
#include <ads_component_rpc.pb.h>

// Defined for utility printing macros
#define DEBUG_ID ADS_DEBUG
#define SERVER_ID ADSSERVC

static sel4gpi_rpc_env_t rpc_env = {
    .request_desc = &AdsMessage_msg,
    .reply_desc = &AdsReturnMessage_msg,
};

int ads_component_client_connect(seL4_CPtr server_ep_cap,
                                 ads_client_context_t *ret_conn)
{
    OSDB_PRINTF("Sending connect request to ADS component\n");

    int error = 0;

    AdsMessage msg = {
        .which_msg = AdsMessage_alloc_tag,
    };

    AdsReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, server_ep_cap, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        ret_conn->badged_server_ep_cspath.capPtr = ret_msg.msg.alloc.slot;
        ret_conn->id = ret_msg.msg.alloc.id;
    }

    return error;
}

int ads_client_attach(ads_client_context_t *conn,
                      void *vaddr,
                      mo_client_context_t *mo_cap,
                      sel4utils_reservation_type_t vmr_type,
                      void **ret_vaddr)
{
    OSDB_PRINTF("Sending attach request to ADS component\n");

    int error = 0;

    AdsMessage msg = {
        .which_msg = AdsMessage_attach_tag,
        .msg.attach = {
            .vaddr = vaddr,
            .type = vmr_type,
        }};

    AdsReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->badged_server_ep_cspath.capPtr, (void *)&msg,
                             1, &mo_cap->badged_server_ep_cspath.capPtr, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        *ret_vaddr = ret_msg.msg.attach.vaddr;
    }

    return error;
}

int ads_client_reserve(ads_client_context_t *conn,
                       void *vaddr,
                       size_t size,
                       size_t page_bits,
                       sel4utils_reservation_type_t vmr_type,
                       ads_vmr_context_t *ret_conn,
                       void **ret_vaddr)
{
    OSDB_PRINTF("Sending reserve request to ADS component\n");

    int error = 0;

    AdsMessage msg = {
        .which_msg = AdsMessage_reserve_tag,
        .msg.reserve = {
            .vaddr = vaddr,
            .type = vmr_type,
            .size = size,
            .page_bits = page_bits,
        }};

    AdsReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->badged_server_ep_cspath.capPtr, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        *ret_vaddr = ret_msg.msg.reserve.vaddr;
        ret_conn->badged_server_ep_cspath.capPtr = ret_msg.msg.reserve.slot;
    }

    return error;
}

int ads_client_attach_to_reserve(ads_vmr_context_t *reservation,
                                 mo_client_context_t *mo,
                                 size_t offset)
{
    OSDB_PRINTF("Sending attach-to-reserve request to ADS component\n");

    int error = 0;

    AdsMessage msg = {
        .which_msg = AdsMessage_attach_reserve_tag,
        .msg.attach_reserve = {
            .offset = offset,
        }};

    AdsReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, reservation->badged_server_ep_cspath.capPtr, (void *)&msg,
                             1, &mo->badged_server_ep_cspath.capPtr, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int ads_client_rm(ads_client_context_t *conn, void *vaddr)
{
    OSDB_PRINTF("Sending remove request to ADS component\n");

    int error = 0;

    AdsMessage msg = {
        .which_msg = AdsMessage_remove_tag,
        .msg.remove = {
            .vaddr = vaddr,
        }};

    AdsReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, conn->badged_server_ep_cspath.capPtr, (void *)&msg,
                             0, NULL, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    return error;
}

int ads_client_bind_cpu(ads_client_context_t *conn, seL4_CPtr cpu_cap)
{
    return 0;
}

int ads_client_copy(ads_client_context_t *src_ads, ads_client_context_t *dst_ads, vmr_config_t *vmr_cfg)
{
    OSDB_PRINTF("Sending copy request to ADS component\n");

    int error = 0;

    AdsMessage msg = {
        .which_msg = AdsMessage_copy_tag,
        .msg.copy = {
            .pages = vmr_cfg->region_pages,
            .type = (uint32_t)vmr_cfg->type,
            .src_vaddr = vmr_cfg->start,
            .dest_vaddr = vmr_cfg->dest_start,
            .share_degree = vmr_cfg->share_mode,
            .provided_mo = vmr_cfg->mo != NULL,
        }};

    AdsReturnMessage ret_msg;

    if (vmr_cfg->mo)
    {
        seL4_CPtr caps[2] = {dst_ads->badged_server_ep_cspath.capPtr, vmr_cfg->mo->badged_server_ep_cspath.capPtr};
        error = sel4gpi_rpc_call(&rpc_env, src_ads->badged_server_ep_cspath.capPtr, (void *)&msg,
                                 2, caps, (void *)&ret_msg);
    }
    else
    {
        error = sel4gpi_rpc_call(&rpc_env, src_ads->badged_server_ep_cspath.capPtr, (void *)&msg,
                                 1, &dst_ads->badged_server_ep_cspath.capPtr, (void *)&ret_msg);
    }
    error |= ret_msg.errorCode;

    return error;
}

/* ======================================= CONVENIENCE FUNCTIONS (NOT PART OF FRAMEWORK) ================================================= */

/**
 * @brief Load an image's ELF into the given ADS
 *
 * @param loadee_ads
 * @param image_name
 * @return int
 */
int ads_client_load_elf(ads_client_context_t *loadee_ads,
                        pd_client_context_t *loadee_pd,
                        const char *image_name,
                        void **ret_entry_point)
{
    OSDB_PRINTF("Sending 'load elf' request to ADS component\n");

    int error = 0;

    AdsMessage msg = {
        .which_msg = AdsMessage_load_elf_tag,
    };

    assert(strlen(image_name) < sizeof(msg.msg.load_elf.image_name));
    strncpy(msg.msg.load_elf.image_name, image_name, sizeof(msg.msg.load_elf.image_name));

    AdsReturnMessage ret_msg;

    error = sel4gpi_rpc_call(&rpc_env, loadee_ads->badged_server_ep_cspath.capPtr, (void *)&msg,
                             1, &loadee_pd->badged_server_ep_cspath.capPtr, (void *)&ret_msg);
    error |= ret_msg.errorCode;

    if (!error)
    {
        *ret_entry_point = ret_msg.msg.load_elf.entry_point;
    }

    return error;
}
