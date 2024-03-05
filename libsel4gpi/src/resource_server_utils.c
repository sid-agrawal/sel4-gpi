#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vspace/vspace.h>

#include <sel4gpi/resource_server_utils.h>

#define SERVER_UTILS "SERVER_UTILS"

// Could use the server's debug function instead
#if RESOURCE_SERVER_DEBUG
#define RESOURCE_SERVER_PRINTF(...)  \
    do                               \
    {                                \
        printf("%s ", SERVER_UTILS); \
        printf(__VA_ARGS__);         \
    } while (0);
#else
#define RESOURCE_SERVER_PRINTF(...)
#endif

#define CHECK_ERROR(error, msg)    \
    do                             \
    {                              \
        if (error != seL4_NoError) \
        {                          \
            ZF_LOGE(SERVER_UTILS   \
                    "%s: %s"       \
                    ", %d.",       \
                    __func__,      \
                    msg,           \
                    error);        \
            return error;          \
        }                          \
    } while (0);

#define CHECK_ERROR_GOTO(check, msg, loc) \
    do                                    \
    {                                     \
        if ((check) != seL4_NoError)      \
        {                                 \
            ZF_LOGE(SERVER_UTILS "%s: %s" \
                                 ", %d.", \
                    __func__,             \
                    msg,                  \
                    check);               \
            error = -1;                   \
            goto loc;                     \
        }                                 \
    } while (0);

int start_resource_server_pd(vka_t *vka,
                             seL4_CPtr gpi_ep,
                             gpi_cap_t rde_type,
                             seL4_CPtr rde_ep,
                             seL4_CPtr rde_pd_cap,
                             char *image_name,
                             seL4_CPtr *server_ep,
                             seL4_CPtr *server_pd_cap)
{
    int error;

    // Create a temporary endpoint for the parent to listen on
    vka_object_t ep_object = {0};
    error = vka_alloc_endpoint(vka, &ep_object);
    CHECK_ERROR(error, "failed to allocate endpoint");

    // Create a new PD
    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(gpi_ep, vka, &pd_os_cap);
    CHECK_ERROR(error, "failed to create new pd");

    if (server_pd_cap)
    {
        *server_pd_cap = pd_os_cap.badged_server_ep_cspath.capPtr;
    }

    // Create a new ADS Cap, which will be in the context of a PD and image
    ads_client_context_t ads_os_cap;
    error = ads_component_client_connect(gpi_ep, vka, &ads_os_cap);
    CHECK_ERROR(error, "failed to create new ads");

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, image_name);
    CHECK_ERROR(error, "failed to load pd image");

    // Copy the parent ep to the new PD
    seL4_Word parent_ep_slot;
    error = pd_client_send_cap(&pd_os_cap, ep_object.cptr, &parent_ep_slot);
    CHECK_ERROR(error, "failed to send parent's ep cap to pd");

    // Copy the RDE to the new PD
    // (XXX) Arya: Todo Badge the RDE with the pd ID
    if (rde_ep > 0)
    {
        RESOURCE_SERVER_PRINTF("SENDING RDE\n");
        error = pd_client_add_rde(&pd_os_cap, rde_ep, rde_pd_cap, rde_type, true);
        CHECK_ERROR(error, "failed to send rde to pd");
    }

    // Start it
    error = pd_client_start(&pd_os_cap, parent_ep_slot); // with this arg.
    CHECK_ERROR(error, "failed to start pd");

    // Wait for it to finish starting
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    // Alloc cap receive path
    cspacepath_t received_cap_path;
    error = vka_cspace_alloc_path(vka, &received_cap_path);
    CHECK_ERROR(error, "failed to alloc receive endpoint");

    seL4_SetCapReceivePath(received_cap_path.root,
                           received_cap_path.capPtr,
                           received_cap_path.capDepth);

    tag = seL4_Recv(ep_object.cptr, NULL);
    int n_caps = seL4_MessageInfo_get_extraCaps(tag);
    CHECK_ERROR(n_caps != 1, "message from server does not contain ep");
    *server_ep = received_cap_path.capPtr;

    // Cleanup temporary endpoint
    // (XXX) Arya: why does this free cause future allocs to break?
    // vka_free_object(vka, &ep_object);

    return 0;
}

int resource_server_spawn_thread(resource_server_context_t *context,
                                 simple_t *parent_simple,
                                 vka_t *parent_vka,
                                 vspace_t *parent_vspace,
                                 seL4_CPtr gpi_ep,
                                 seL4_CPtr parent_ep,
                                 seL4_CPtr ads_ep,
                                 uint8_t priority,
                                 char *thread_name,
                                 int (*main_fn)())
{
    RESOURCE_SERVER_PRINTF("Starting resource server thread\n");

    int error;
    cspacepath_t parent_cspace_cspath;
    seL4_MessageInfo_t tag;

    if (parent_simple == NULL || parent_vka == NULL || parent_vspace == NULL)
    {
        return seL4_InvalidArgument;
    }

    context->server_vka = parent_vka;
    context->gpi_ep = gpi_ep;
    context->parent_ep = parent_ep;
    context->ads_conn = malloc(sizeof(ads_client_context_t));
    context->ads_conn->badged_server_ep_cspath.capPtr = ads_ep;

    /* Get a CPtr to the parent's root cnode. */
    vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

    /* Allocate the Endpoint that the server will be listening on. */
    vka_object_t server_ep_obj;
    error = vka_alloc_endpoint(parent_vka, &server_ep_obj);
    CHECK_ERROR(error, "failed in vka_alloc_endpoint");
    context->server_ep = server_ep_obj.cptr;

    RESOURCE_SERVER_PRINTF("Allocated endpoint\n");

    /* Configure thread */
    sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                             parent_cspace_cspath.root,
                                                             seL4_NilData,
                                                             context->server_ep,
                                                             priority);

    sel4utils_thread_t thread;
    error = sel4utils_configure_thread_config(parent_vka,
                                              parent_vspace,
                                              parent_vspace,
                                              config,
                                              &thread);
    CHECK_ERROR_GOTO(error, "sel4utils_configure_thread failed", out);

    RESOURCE_SERVER_PRINTF("Starting resource server thread\n");
    NAME_THREAD(thread.tcb.cptr, thread_name);
    error = sel4utils_start_thread(&thread,
                                   (sel4utils_thread_entry_fn)main_fn,
                                   NULL, NULL, 1);
    CHECK_ERROR_GOTO(error, "sel4utils_start_thread failed", out);

    return 0;

out:
    RESOURCE_SERVER_PRINTF("spawn_thread: Server ran into an error.\n");
    vka_free_object(parent_vka, &server_ep_obj);
    return error;
}

int resource_server_start(resource_server_context_t *context,
                          ads_client_context_t *ads_conn,
                          pd_client_context_t *pd_conn,
                          seL4_CPtr gpi_ep,
                          seL4_CPtr parent_ep,
                          int (*main_fn)())
{
    seL4_Error error;

    context->server_vka = NULL;
    context->gpi_ep = gpi_ep;
    context->ads_conn = ads_conn;
    context->pd_conn = pd_conn;
    context->parent_ep = parent_ep;

    /* Allocate the Endpoint that the server will be listening on. */
    error = pd_client_alloc_ep(context->pd_conn, &context->server_ep);
    CHECK_ERROR(error, "Failed to allocate endpoint for resource server");
    RESOURCE_SERVER_PRINTF("Allocated server ep at %d\n", (int)context->server_ep);

    RESOURCE_SERVER_PRINTF("Going to main function\n");
    return main_fn();
}

seL4_MessageInfo_t resource_server_recv(resource_server_context_t *context,
                                        seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(context->server_ep,
                    sender_badge_ptr,
                    context->mcs_reply);
}

void resource_server_reply(resource_server_context_t *context,
                           seL4_MessageInfo_t tag)
{
    api_reply(context->mcs_reply, tag);
}

int resource_server_next_slot(resource_server_context_t *context,
                              seL4_CPtr *slot)
{
    if (context->server_vka != NULL)
    {
        return vka_cspace_alloc(context->server_vka, slot);
    }
    else
    {
        return pd_client_next_slot(context->pd_conn, slot);
    }
    return vka_cspace_alloc(context->server_vka, slot);
}

int resource_server_free_slot(resource_server_context_t *context,
                              seL4_CPtr slot)
{
    if (context->server_vka != NULL)
    {
        // First try to delete slot contents,
        // ignore error if slot is already empty
        cspacepath_t path;
        vka_cspace_make_path(context->server_vka, slot, &path);
        vka_cnode_delete(&path);
        vka_cspace_free(context->server_vka, slot);
    }
    else
    {
        return pd_client_free_slot(context->pd_conn, slot);
    }
}

int resource_server_badge_ep(resource_server_context_t *context,
                             seL4_Word badge, seL4_CPtr *badged_ep)
{
    if (context->server_vka != NULL)
    {
        cspacepath_t src, dest;
        vka_cspace_make_path(context->server_vka,
                             context->server_ep, &src);
        int error = vka_cspace_alloc_path(context->server_vka,
                                          &dest);
        if (error)
        {
            return error;
        }

        error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);

        *badged_ep = dest.capPtr;
        return error;
    }
    else
    {
        return pd_client_badge_ep(context->pd_conn,
                                  context->server_ep,
                                  badge, badged_ep);
    }
}

int resource_server_main(resource_server_context_t *context,
                         seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr))
{
    seL4_MessageInfo_t tag;
    seL4_Error error = 0;
    seL4_Word sender_badge;
    cspacepath_t received_cap_path;
    received_cap_path.root = PD_CAP_ROOT;
    received_cap_path.capDepth = PD_CAP_DEPTH;

    while (1)
    {
        /* Alloc cap receive slot*/
        error = resource_server_next_slot(context, &received_cap_path.capPtr);
        CHECK_ERROR_GOTO(error, "failed to alloc cap receive slot", exit_main);

        seL4_SetCapReceivePath(
            received_cap_path.root,
            received_cap_path.capPtr,
            received_cap_path.capDepth);

        /* Receive a message */
        tag = resource_server_recv(context, &sender_badge);
        seL4_MessageInfo_t reply_tag = request_handler(tag, sender_badge, received_cap_path.capPtr);

        /* Reply to message */
        resource_server_reply(context, reply_tag);
        resource_server_free_slot(context, received_cap_path.capPtr);
    }

exit_main:
    RESOURCE_SERVER_PRINTF("Suspending resource server");
    return -1;
}

/**
 * Attach a MO from a client request to the server's ADS
 * @param mo_cap The MO cap to attach
 * @param vaddr Returns the vaddr where MO was attached
 */
int resource_server_attach_mo(resource_server_context_t *context,
                              seL4_CPtr mo_cap,
                              void **vaddr)
{
    // (XXX) Arya: Track the MO so we can unattach and free it later
    int error = 0;
    mo_client_context_t mo_conn;

    CHECK_ERROR(mo_cap == 0, "client did not attach MO for read/write op");

    mo_conn.badged_server_ep_cspath.capPtr = mo_cap;
    error = ads_client_attach(context->ads_conn,
                              NULL,
                              &mo_conn,
                              vaddr);
    CHECK_ERROR(error, "failed to attach client's MO to ADS");

    return error;
}

int resource_server_get_rr(seL4_CPtr server_ep,
                           seL4_CPtr resource,
                           mo_client_context_t *mo_conn,
                           void *mo_vaddr,
                           size_t size,
                           rr_state_t **ret_rr_state)
{
    RESOURCE_SERVER_PRINTF("requesting resource relations for cap %d\n", (int)resource);

    // Send IPC to resource server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2, RSMSGREG_EXTRACT_RR_REQ_END);
    seL4_SetMR(RSMSGREG_FUNC, RS_FUNC_GET_RR_REQ);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE, size);
    seL4_SetCap(0, mo_conn->badged_server_ep_cspath.capPtr);
    seL4_SetCap(1, resource);
    tag = seL4_Call(server_ep, tag);

    // Adjust rr state's row pointer if successful
    int result = seL4_MessageInfo_get_label(tag);
    if (result == seL4_NoError)
    {
        rr_state_t *rr_state = (rr_state_t *)mo_vaddr;
        rr_state->csv_rows = (csv_rr_row_t *)(mo_vaddr + sizeof(rr_state_t));
        *ret_rr_state = rr_state;

        RESOURCE_SERVER_PRINTF("TEMPA %d\n", rr_state->csv_rows_len);
    }

    return result;
}