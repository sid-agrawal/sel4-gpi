#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4gpi/pd_utils.h>
#include <sel4utils/process.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/error_handle.h>

#include <sel4runtime.h>

#include <kvstore_shared.h>
#include <kvstore_client.h>
#include <kvstore_server.h>
#include <kvstore_server_rpc.pb.h>
#include <fs_client.h>
#include <malloc.h>

static kvstore_mode_t mode;
static seL4_CPtr server_ep;
static ads_client_context_t kvserv_ads;
static ads_client_context_t client_ads_conn;
static cpu_client_context_t self_cpu_conn;
extern global_xv6fs_client_context_t xv6fs_client;

static sel4gpi_rpc_env_t rpc_client = {
    .request_desc = &KvstoreMessage_msg,
    .reply_desc = &KvstoreReturnMessage_msg,
};

int kvstore_client_swap_ads_lib(void)
{
    int error = 0;
    error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
    if (error)
    {
        ZF_LOGE("failed to swap ADS to kvstore server");
        return error;
    }
    return error;
}

int kvstore_client_swap_ads_app(void)
{
    int error = 0;
    error = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
    if (error)
    {
        ZF_LOGE("failed to swap ADS to app");
        return error;
    }
    return error;
}

static int configure_separate_ads()
{
    int error = 0;

    /* new ADS*/
    sel4gpi_runnable_t runnable = {0};

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    GOTO_IF_COND(ads_rde == seL4_CapNull, "Can't make new ADS, no ADS RDE\n");

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    GOTO_IF_COND(mo_rde == seL4_CapNull, "No MO RDE found\n");

    ads_client_context_t self_ads_conn = sel4gpi_get_ads_conn();

    error = ads_component_client_connect(ads_rde, &runnable.ads);
    GOTO_IF_ERR(error, "failed to allocate a new ADS");

    ads_config_t other_ads_cfg = {0};

    /* heap: deep copied */
    // (XXX) Linh: this currently needs to be deep-copied due to malloc bookkeeping requirements
    mo_client_context_t heap_mo = {0};
    error = mo_component_client_connect(mo_rde, DEFAULT_HEAP_PAGES, MO_PAGE_BITS, &heap_mo);
    GOTO_IF_ERR(error, "Failed to allocate MO for the heap\n");

    sel4gpi_add_vmr_config(&other_ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_HEAP,
                           (void *)PD_HEAP_LOC, NULL, DEFAULT_HEAP_PAGES, MO_PAGE_BITS, &heap_mo);

    /* per-PD OSmosis data: shallow copied */
    sel4gpi_add_vmr_config(&other_ads_cfg, GPI_SHARED, SEL4UTILS_RES_TYPE_GENERIC, sel4runtime_get_osm_shared_data(),
                           NULL, 1, MO_PAGE_BITS, NULL);

    /* ELF data section: deep copied */
    void *elf_data_va = NULL;
    size_t elf_data_pages = 0;
    size_t elf_page_bits = 0;
    mo_client_context_t elf_data_mo = {0};
    error = ads_client_get_reservation(&self_ads_conn, SEL4UTILS_RES_TYPE_DATA,
                                       &elf_data_va, &elf_data_pages, &elf_page_bits);
    GOTO_IF_ERR(error, "Failed to get ADS data reservation for ELF data section\n");
    error = mo_component_client_connect(mo_rde, elf_data_pages, elf_page_bits, &elf_data_mo);
    GOTO_IF_ERR(error, "Failed to allocate MO for ELF data\n");

    sel4gpi_add_vmr_config(&other_ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DATA, elf_data_va, NULL, elf_data_pages,
                           elf_page_bits, &elf_data_mo);

    /* ELF code section: shallow copy by type */
    sel4gpi_add_vmr_config(&other_ads_cfg, GPI_SHARED, SEL4UTILS_RES_TYPE_CODE, NULL, NULL, 0, 0, NULL);

    /* stack: shallow copy by type */
    sel4gpi_add_vmr_config(&other_ads_cfg, GPI_SHARED, SEL4UTILS_RES_TYPE_STACK, NULL, NULL, 0, 0, NULL);

    /* IPC buffer: shallow copy by type */
    sel4gpi_add_vmr_config(&other_ads_cfg, GPI_SHARED, SEL4UTILS_RES_TYPE_IPC_BUF, NULL, NULL, 0, 0, NULL);

    /* These deep-copying calls must happen here due to malloc bookkeeping */
    error = sel4gpi_copy_data_to_mo((void *)PD_HEAP_LOC, DEFAULT_HEAP_PAGES * SIZE_BITS_TO_BYTES(MO_PAGE_BITS), &heap_mo);
    GOTO_IF_ERR(error, "Failed to deep-copy heap\n");

    error = sel4gpi_copy_data_to_mo(elf_data_va, elf_data_pages * SIZE_BITS_TO_BYTES(elf_page_bits), &elf_data_mo);
    GOTO_IF_ERR(error, "Failed to deep copy ELF data region\n");

    error = sel4gpi_ads_configure(&other_ads_cfg, &runnable, NULL, NULL);
    GOTO_IF_ERR(error, "Failed to configure other ADS\n");
    kvserv_ads = runnable.ads;

    linked_list_destroy(other_ads_cfg.vmr_cfgs, true);

    self_cpu_conn = sel4gpi_get_cpu_conn();
    error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
    GOTO_IF_ERR(error, "failed to swap ADS to kvstore server\n");

    // we need to clear the FS client instance, so the lib starts with a fresh one
    memset(&xv6fs_client, 0, sizeof(global_xv6fs_client_context_t));
    error = kvstore_server_init();
    GOTO_IF_ERR(error, "Failed to initialize kvstore\n");

    // need to set this variable twice since we're in a different vspace
    client_ads_conn = sel4gpi_get_ads_conn();
    self_cpu_conn = sel4gpi_get_cpu_conn();
    error = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
    FATAL_IF_ERR(error, "Failed to switch back to client ADS"); // fatal because we can't continue in the wrong ADS

err_goto:
    return error;
}

int kvstore_client_configure(kvstore_mode_t kvstore_mode, seL4_CPtr ep)
{
    int error = 0;
    mode = kvstore_mode;

    switch (kvstore_mode)
    {
    case SAME_THREAD:
        // This PD will be a local kvstore, initialize
        error = kvstore_server_init();
        break;
    case SEPARATE_ADS:
        error = configure_separate_ads();
        break;
    case SEPARATE_THREAD:
        error = kvstore_server_start_thread(&server_ep);
        break;
    case SEPARATE_PROC:
        // This PD will send kvstore requests to another PD
        server_ep = ep;
        break;
    }

    return error;
}

int kvstore_client_create_kvstore(seL4_CPtr *dest)
{
    seL4_Error error = 0;

    if (mode == SEPARATE_PROC || mode == SEPARATE_THREAD)
    {
        KvstoreMessage request = {
            .magic = KVSTORE_RPC_MAGIC,
            .which_msg = KvstoreMessage_create_tag};

        KvstoreReturnMessage reply = {0};

        error = sel4gpi_rpc_call(&rpc_client, server_ep, &request, 0, NULL, &reply);

        error |= reply.errorCode;

        if (error == seL4_NoError) {
            *dest = reply.msg.alloc.dest;
        }
    }
    else {
        // No need to create kvstore resource when using the same address space
    }

    return error;
}

int kvstore_client_set(seL4_CPtr kvstore_ep, seL4_Word key, seL4_Word value)
{
    seL4_Error error;

    if (mode == SEPARATE_PROC || mode == SEPARATE_THREAD)
    {
        KvstoreMessage request = {
            .magic = KVSTORE_RPC_MAGIC,
            .which_msg = KvstoreMessage_set_tag,
            .msg.set = {
                .key = key,
                .val = value,
            }};

        KvstoreReturnMessage reply = {0};

        error = sel4gpi_rpc_call(&rpc_client, kvstore_ep, &request, 0, NULL, &reply);

        error |= reply.errorCode;
    }
    else if (mode == SEPARATE_ADS)
    {
        error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
        if (error)
        {
            ZF_LOGE("failed to swap ADS to kvstore server");
            return error;
        }

        error = kvstore_server_set(key, value);
        ZF_LOGE_IF(error, "kvstore_server_set failed");

        // don't overwrite the error value from the actual server command
        int swap_err = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
        ZF_LOGF_IF(swap_err, "Failed to swap back to client ADS"); // fatal because we can't continue in the wrong ADS
    }
    else
    {
        error = kvstore_server_set(key, value);
    }

    return error;
}

int kvstore_client_get(seL4_CPtr kvstore_ep, seL4_Word key, seL4_Word *value)
{
    seL4_Error error;

    if (mode == SEPARATE_PROC || mode == SEPARATE_THREAD)
    {
        KvstoreMessage request = {
            .magic = KVSTORE_RPC_MAGIC,
            .which_msg = KvstoreMessage_get_tag,
            .msg.set = {
                .key = key,
            }};

        KvstoreReturnMessage reply = {0};

        error = sel4gpi_rpc_call(&rpc_client, kvstore_ep, &request, 0, NULL, &reply);

        error |= reply.errorCode;

        if (error == seL4_NoError) {
            *value = reply.msg.get.val;
        }
    }
    else if (mode == SEPARATE_ADS)
    {
        error = cpu_client_change_vspace(&self_cpu_conn, &kvserv_ads);
        if (error)
        {
            ZF_LOGE("failed to swap ADS to kvstore server");
            return error;
        }
        error = kvstore_server_get(key, value);
        ZF_LOGE_IF(error, "kvstore_server_get failed");

        // don't overwrite the error value from the actual server command
        int swap_err = cpu_client_change_vspace(&self_cpu_conn, &client_ads_conn);
        ZF_LOGF_IF(swap_err, "Failed to swap back to client ADS"); // fatal because we can't continue in the wrong ADS
    }
    else
    {
        error = kvstore_server_get(key, value);
    }

    return error;
}
