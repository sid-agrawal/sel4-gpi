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
#include <sel4gpi/resource_registry.h>
#include <sel4gpi/resource_server_utils.h>

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
                             gpi_space_id_t rde_id,
                             char *image_name,
                             seL4_CPtr *server_pd_cap,
                             gpi_space_id_t *space_id)
{
    return start_resource_server_pd_args(rde_type, rde_id, image_name, NULL, 0, server_pd_cap, space_id);
}

int start_resource_server_pd_args(gpi_cap_t rde_type,
                                  gpi_space_id_t rde_id,
                                  char *image_name,
                                  seL4_Word *args_input,
                                  uint32_t argc_input,
                                  seL4_CPtr *server_pd_cap,
                                  gpi_space_id_t *space_id)
{
    int error;

    // Current pd
    pd_client_context_t current_pd = sel4gpi_get_pd_conn();

    // Create a temporary endpoint for the parent to listen on
    ep_client_context_t ep_conn;
    error = sel4gpi_alloc_endpoint(&ep_conn);
    CHECK_ERROR(error, "failed to allocate endpoint");

    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process(image_name, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    error = cfg == NULL;
    CHECK_ERROR(error, "failed to configure process");

    if (server_pd_cap)
    {
        *server_pd_cap = runnable.pd.ep;
    }

    // Copy the parent ep to the new PD
    seL4_CPtr parent_ep_slot;
    error = pd_client_send_cap(&runnable.pd, ep_conn.ep, &parent_ep_slot);
    CHECK_ERROR(error, "failed to send parent's ep cap to pd");

    // Copy the RDE to the new PD
    if (rde_id != 0)
    {
        sel4gpi_add_rde_config(cfg, rde_type, rde_id);
    }

    // By default, resource servers need to be able to create EPs
    sel4gpi_add_rde_config(cfg, GPICAP_TYPE_EP, BADGE_SPACE_ID_NULL);

    // Setup the args
    int argc = 2 + argc_input;
    seL4_Word args[argc];
    args[0] = parent_ep_slot;
    args[1] = current_pd.id;

    for (int i = 0; i < argc_input; i++)
    {
        args[i + 2] = args_input[i];
    }

    // Start it
    error = sel4gpi_prepare_pd(cfg, &runnable, argc, args);
    CHECK_ERROR(error, "failed to prepare pd");

    error = sel4gpi_start_pd(&runnable);
    CHECK_ERROR(error, "failed to start pd");

    // Wait for it to finish starting
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_conn.raw_endpoint, NULL);
    int n_caps = seL4_MessageInfo_get_extraCaps(tag);
    error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "message from server is a failure");

    if (space_id)
    {
        *space_id = seL4_GetMR(0);
    }

    // Cleanup temporary endpoint
    error = ep_component_client_disconnect(&ep_conn);
    CHECK_ERROR(error, "failed to delete temporary endpoint");

    sel4gpi_config_destroy(cfg);

    return 0;
}