#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/pd_utils.h>
#include <sel4gpi/pd_creation.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/resource_server_remote_utils.h>

#define CHECK_ERROR(error, msg)    \
    do                             \
    {                              \
        if (error != seL4_NoError) \
        {                          \
            ZF_LOGE("%s"           \
                    ", %d.",       \
                    msg,           \
                    error);        \
            return error;          \
        }                          \
    } while (0);

int start_resource_server_pd(gpi_cap_t rde_type,
                             uint64_t rde_id,
                             char *image_name,
                             seL4_CPtr *server_pd_cap,
                             uint64_t *space_id)
{
    int error;

    // Current pd
    pd_client_context_t current_pd = sel4gpi_get_pd_conn();

    // Create a temporary endpoint for the parent to listen on
    seL4_CPtr ep;
    error = pd_client_alloc_ep(&current_pd, &ep);
    CHECK_ERROR(error, "failed to allocate endpoint");

    pd_client_context_t server_pd;
    pd_config_t *cfg = sel4gpi_configure_process(image_name, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &server_pd);
    error = cfg == NULL;
    CHECK_ERROR(error, "failed to configure process");

    if (server_pd_cap)
    {
        *server_pd_cap = server_pd.badged_server_ep_cspath.capPtr;
    }

    // Copy the parent ep to the new PD
    seL4_Word parent_ep_slot;
    error = pd_client_send_cap(&server_pd, ep, &parent_ep_slot);
    CHECK_ERROR(error, "failed to send parent's ep cap to pd");

    // Copy the RDE to the new PD
    if (rde_id != 0)
    {

        RESOURCE_SERVER_PRINTF("SENDING RDE\n");
        error = pd_client_share_rde(&server_pd, rde_type, rde_id);
        CHECK_ERROR(error, "failed to send rde to pd");
    }

    // Setup the args
    int argc = 2;
    seL4_Word args[argc];
    args[0] = parent_ep_slot;
    args[1] = current_pd.id;

    // Start it
    sel4gpi_runnable_t runnable = {.pd = server_pd};
    error = sel4gpi_start_pd(cfg, &runnable, argc, args);
    CHECK_ERROR(error, "failed to start pd");

    // Wait for it to finish starting
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep, NULL);
    int n_caps = seL4_MessageInfo_get_extraCaps(tag);
    error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "message from server is a failure");

    if (space_id)
    {
        *space_id = seL4_GetMR(0);
    }

    // Cleanup temporary endpoint
    // (XXX) Arya: why does this free cause future allocs to break?
    // vka_free_object(vka, &ep_object);
    sel4gpi_config_destroy(cfg);

    return 0;
}

int resource_server_client_get_rr(seL4_CPtr server_ep,
                                  seL4_Word space_id,
                                  seL4_Word res_id,
                                  seL4_Word pd_id,
                                  seL4_Word server_pd_id,
                                  void *remote_vaddr,
                                  void *local_vaddr,
                                  size_t size,
                                  model_state_t **ret_state)
{
    RESOURCE_SERVER_PRINTF("requesting resource relations for ID 0x%lx\n", res_id);
    RESOURCE_SERVER_PRINTF("Shared mem local addr: %p, remote addr: %p\n", local_vaddr, remote_vaddr);

    // Send IPC to resource server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, RSMSGREG_EXTRACT_RR_REQ_END);
    seL4_SetMR(RSMSGREG_FUNC, RS_FUNC_GET_RR_REQ);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR, (seL4_Word)remote_vaddr);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE, size);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_SPACE, space_id);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_ID, res_id);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_PD_ID, pd_id);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_RS_PD_ID, server_pd_id);
    tag = seL4_Call(server_ep, tag);

    // Adjust result state's pointers if successful
    int result = seL4_MessageInfo_get_label(tag);
    if (result == seL4_NoError)
    {
        model_state_t *model_state = (model_state_t *)local_vaddr;

        gpi_model_state_component_t *old_mem_start = model_state->mem_start;
        model_state->mem_start = (gpi_model_state_component_t *)(local_vaddr + sizeof(model_state_t));
        model_state->mem_ptr = model_state->mem_ptr - old_mem_start + model_state->mem_start;

        *ret_state = model_state;
    }

    return result;
}

int resource_server_client_new_ns(seL4_CPtr server_ep,
                                  uint64_t *ns_id)
{
    RESOURCE_SERVER_PRINTF("Requesting new namespace from server ep (%d)\n", (int)server_ep);

    // Send IPC to resource server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, RSMSGREG_NEW_NS_REQ_END);
    seL4_SetMR(RSMSGREG_FUNC, RS_FUNC_NEW_NS_REQ);
    tag = seL4_Call(server_ep, tag);

    int result = seL4_MessageInfo_get_label(tag);
    if (result == seL4_NoError)
    {
        *ns_id = seL4_GetMR(RSMSGREG_NEW_NS_ACK_ID);
    }

    return result;
}