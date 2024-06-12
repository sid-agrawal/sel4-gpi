#include <sel4runtime.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/pd_utils.h>

static seL4_CPtr next_reply_cap_slot;

pd_client_context_t sel4gpi_get_pd_conn(void)
{
    pd_client_context_t conn = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->pd_conn;
    return conn;
}

ads_client_context_t sel4gpi_get_ads_conn(void)
{
    ads_client_context_t conn = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->ads_conn;
    return conn;
}

cpu_client_context_t sel4gpi_get_cpu_conn(void)
{
    cpu_client_context_t conn = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->cpu_conn;
    return conn;
}

uint64_t sel4gpi_get_binded_ads_id(void)
{
    uint64_t id = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->ads_conn.id;
    return id;
}

seL4_CPtr sel4gpi_get_cspace_root(void)
{
    seL4_CPtr slot = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->cspace_root;
    return slot;
}

gpi_cap_t sel4gpi_get_resource_type_code(char *type_name)
{
    osm_pd_shared_data_t *shared_data = (osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data();
    for (int i = 0; i < GPICAP_TYPE_MAX; i++)
    {
        if (strcmp(shared_data->type_names[i], type_name) == 0)
        {
            return i;
        }
    }

    // This is not good
    return 0;
}

char *sel4gpi_get_resource_type_name(gpi_cap_t type)
{
    osm_pd_shared_data_t *shared_data = (osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data();
    return shared_data->type_names[type];
}

seL4_CPtr sel4gpi_get_rde(int type)
{
    seL4_CPtr slot = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->rde[type][0].slot_in_PD;

    if (slot == seL4_CapNull)
    {
        printf(COLORIZE("Warning: could not find RDE (type: %d) for PD (%ld)\n", MAGENTA),
               type, sel4gpi_get_pd_conn().id);
    }

    return slot;
}

uint64_t sel4gpi_get_default_space_id(int type)
{
    uint64_t space_id = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->rde[type][0].space_id;

    return space_id;
}

seL4_CPtr sel4gpi_get_rde_by_space_id(uint32_t space_id, gpi_cap_t type)
{
    assert(type != GPICAP_TYPE_NONE && type != GPICAP_TYPE_MAX);
    osm_pd_shared_data_t *shared_data = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data());

    if (space_id == RESSPC_ID_NULL)
    {
        return sel4gpi_get_rde(type);
    }

    int idx = -1;

    for (int i = 0; i < MAX_NS_PER_RDE; i++)
    {
        if (shared_data->rde[type][i].space_id == space_id)
        {
            return shared_data->rde[type][i].slot_in_PD;
        }
    }

    printf(COLORIZE("Warning: could not find RDE (type: %d, space: %d) for PD (%ld)\n", MAGENTA),
           type, space_id, sel4gpi_get_pd_conn().id);
    return seL4_CapNull;
}

void sel4gpi_debug_print_rde(void)
{
    osm_pd_shared_data_t *shared_data = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data());
    printf("RDEs ------------------------------------ \n");
    for (int i = GPICAP_TYPE_NONE + 1; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            if (shared_data->rde[i][j].type.type != GPICAP_TYPE_NONE)
            {
                printf("type:%d \tid: %d\n", shared_data->rde[i][j].type.type, shared_data->rde[i][j].space_id);
            }
        }
    }
}

void sel4gpi_store_reply_cap(void)
{
    int error;

    // Save the reply cap in a previously-allocated slot
    error = seL4_CNode_SaveCaller(
        PD_CAP_ROOT,
        next_reply_cap_slot,
        PD_CAP_DEPTH);

    // Update the shared data
    ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->reply_cap = next_reply_cap_slot;

err_goto:
    return;
}

seL4_CPtr sel4gpi_get_reply_cap(void)
{
    return ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->reply_cap;
}

void sel4gpi_clear_reply_cap(void)
{
    int error = 0;
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
    seL4_CPtr slot = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->reply_cap;

    // Set the data to null first in case we are killed while the slot is being freed
    ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data())->reply_cap = seL4_CapNull;

    // Free the old slot
    if (slot != seL4_CapNull)
    {
        error = pd_client_free_slot(&pd_conn, slot);
        GOTO_IF_ERR(error, "Failed to free slot for reply cap\n");
    }

    // Setup a new slot
    error = pd_client_next_slot(&pd_conn, &next_reply_cap_slot);
    GOTO_IF_ERR(error, "Failed to allocate slot for reply cap\n");

err_goto:
    return;
}

static void sel4gpi_exit_cb(int code)
{
    /* Notify the pd component to destruct this PD */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
    pd_client_exit(&pd_conn);
}

void sel4gpi_set_exit_cb(void)
{
    /* Set exit handler */
    sel4runtime_set_exit(sel4gpi_exit_cb);
}

void *sel4gpi_get_vmr(ads_client_context_t *vmr_rde, int num_pages, void *vaddr,
                      sel4utils_reservation_type_t vmr_type, mo_client_context_t *ret_mo)
{
    int error;
    GOTO_IF_COND(num_pages <= 0, "Invalid VMR pages specified: %d\n", num_pages);
    pd_client_context_t self_pd = sel4gpi_get_pd_conn();

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);

    mo_client_context_t mo;
    error = mo_component_client_connect(mo_rde, num_pages, &mo);
    GOTO_IF_ERR(error, "failed to allocate MO\n");

    void *new_vaddr;
    error = ads_client_attach(vmr_rde, vaddr, &mo, vmr_type, &new_vaddr);
    GOTO_IF_ERR(error, "failed to attach MO\n");

    if (ret_mo)
    {
        *ret_mo = mo;
    }

    return new_vaddr;

err_goto:
    return NULL;
}

int sel4gpi_destroy_vmr(ads_client_context_t *vmr_rde, void *vaddr, mo_client_context_t *mo)
{
    int error;

    error = ads_client_rm(vmr_rde, vaddr);
    GOTO_IF_ERR(error, "failed to remove MO from ADS\n");

    error = mo_component_client_disconnect(mo);
    GOTO_IF_ERR(error, "failed to disconnect MO\n");

err_goto:
    return error;
}
