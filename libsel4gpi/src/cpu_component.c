/**
 * @file cpu_component.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the cpu server API from cpu_component.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <autoconf.h>

#include <stdio.h>
#include <string.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <utils/arith.h>
#include <utils/ansi.h>
#include <sel4utils/api.h>
#include <sel4utils/strerror.h>

#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/cpu_component.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/error_handle.h>

// Defined for utility printing macros
#define DEBUG_ID CPU_DEBUG
#define SERVER_ID CPUSERVS

resource_component_context_t *get_cpu_component(void)
{
    return &get_gpi_server()->cpu_component;
}

// Called when an item from the CPU registry is deleted
static void on_cpu_registry_delete(resource_server_registry_node_t *node_gen)
{
    cpu_component_registry_entry_t *node = (cpu_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying CPU (%d)\n", node->cpu.id);

    cpu_destroy(&node->cpu);
}

static seL4_MessageInfo_t handle_cpu_allocation(seL4_Word sender_badge)
{
    OSDB_PRINTF("Got CPU allocation request from %lx\n", sender_badge);
    badge_print(sender_badge);

    int error = 0;
    seL4_MessageInfo_t reply_tag;
    seL4_CPtr ret_cap;
    cpu_component_registry_entry_t *new_entry;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    error = resource_component_allocate(get_cpu_component(), client_id, BADGE_OBJ_ID_NULL, false, NULL,
                                        (resource_server_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new CPU object\n");

    seL4_SetCap(0, ret_cap);

    OSDB_PRINTF("Allocated new CPU (%d)\n", new_entry->cpu.id);

    reply_tag = seL4_MessageInfo_new(error, 0, 1, CPUMSGREG_CONNECT_ACK_END);
    return reply_tag;

err_goto:
    reply_tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_CONNECT_ACK_END);
    return reply_tag;
}

static seL4_MessageInfo_t handle_start_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got start request from client badge %lx.\n", sender_badge);

    int error = 0;
    /* Find the client */
    cpu_component_registry_entry_t *client_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find CPU (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word init_stack = seL4_GetMR(CPUMSGREG_START_INIT_STACK_ADDR);
    seL4_Word arg0 = seL4_GetMR(CPUMSGREG_START_ARG0);

    error = cpu_start(&client_data->cpu,
                      (void *)seL4_GetMR(CPUMSGREG_START_FUNC_VADDR),
                      (void *)init_stack);
    SERVER_GOTO_IF_ERR(error, "Failed to start CPU\n");

err_goto:
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_START_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_START_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_config_req(seL4_Word sender_badge,
                                            seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got CPU config request from client badge %lx\n", sender_badge);

    int error = 0;

    // We should have 2 capsunwrapped: the PD, and the ADS
    SERVER_GOTO_IF_COND(seL4_MessageInfo_ptr_get_capsUnwrapped(&old_tag) < 2, "Config request requires 2 capsUnwrapped\n");

    /* Find the client */
    cpu_component_registry_entry_t *client_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find CPU (%ld)\n", get_object_id_from_badge(sender_badge));

    pd_component_registry_entry_t *pd_data = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_pd_component(), seL4_GetBadge(0));
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(seL4_GetBadge(0)));

    /* Get Fault EP */
    seL4_CPtr fault_ep = seL4_GetMR(CPUMSGREG_CONFIG_FAULT_EP);

    /* Get IPC buf addr */
    seL4_Word ipc_buf_addr = seL4_GetMR(CPUMSGREG_CONFIG_IPC_BUF_ADDR);

    /* get cnode guard*/
    seL4_Word cnode_guard = seL4_GetMR(CPUMSGREG_CONFIG_CNODE_GUARD);

    /* Get the vspace for the ads */
    seL4_Word ads_cap_badge = seL4_GetBadge(1);
    ads_component_registry_entry_t *asre = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), ads_cap_badge);
    SERVER_GOTO_IF_COND(asre == NULL, "Couldn't find ADS (%ld)\n", get_object_id_from_badge(ads_cap_badge));

    vspace_t *ads_vspace = asre->ads.vspace;

    /* Find the IPC MO, if it exists (OK if it doesn't exist) */
    seL4_Word ipc_buf_mo_badge = seL4_GetBadge(2);
    mo_component_registry_entry_t *ipc_mo_data = (mo_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_mo_component(), ipc_buf_mo_badge);

    seL4_CPtr ipc_buf_frame = ipc_mo_data == NULL ? seL4_CapNull : ipc_mo_data->mo.frame_caps_in_root_task[0];

    /* Configure the vspace */
    seL4_CNode cspace_root = pd_data->pd.proc.cspace.cptr;

    error = cpu_config_vspace(&client_data->cpu,
                              get_cpu_component()->server_vka,
                              ads_vspace,
                              cspace_root,
                              cnode_guard,
                              fault_ep,
                              ipc_buf_frame,
                              ipc_buf_addr);

    // (XXX) Arya: here, the issue with va_args was seen before adding the CPU object ID to the format
    SERVER_GOTO_IF_ERR(error, "Failed to configure vspace for CPU (%ld)\n", get_object_id_from_badge(sender_badge));

    client_data->cpu.binded_ads_id = asre->ads.id;

    /* Configure the vspace */
    error = pd_configure(&pd_data->pd, "TEMP", &asre->ads, &client_data->cpu);
    SERVER_GOTO_IF_ERR(error, "Failed to configure PD\n");

    OSDB_PRINTF("Finished configuring CPU\n");

err_goto:
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_CONFIG_ACK_END);
    return tag;
}

static seL4_MessageInfo_t handle_change_vspace_req(seL4_Word sender_badge,
                                                   seL4_MessageInfo_t old_tag,
                                                   seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got change vspace from client badge %lx\n", sender_badge);

    int error = 0;

    // We should have 1 capsunwrapped: the ADS
    SERVER_GOTO_IF_COND(seL4_MessageInfo_ptr_get_capsUnwrapped(&old_tag) < 1, "Change vspace request requires 1 capsUnwrapped\n");

    /* Find the client */
    cpu_component_registry_entry_t *client_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find CPU (%ld)\n", get_object_id_from_badge(sender_badge));

    /* Find the PD */
    pd_component_registry_entry_t *pd_data = (pd_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_pd_component(), get_client_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%ld)\n", get_client_id_from_badge(sender_badge));

    /* Find the ADS */
    seL4_Word ads_cap_badge = seL4_GetBadge(0);
    ads_component_registry_entry_t *ads_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), ads_cap_badge);
    SERVER_GOTO_IF_COND(ads_data == NULL, "Couldn't find ADS (%ld)\n", get_object_id_from_badge(ads_cap_badge));
    vspace_t *ads_vspace = ads_data->ads.vspace;

    // Change vspace
    error = cpu_change_vspace(&client_data->cpu,
                              get_cpu_component()->server_vka,
                              ads_vspace);

    SERVER_GOTO_IF_ERR(error, "Failed to change vspace\n");

    // Update the PD object with the new ADS
    // (XXX) Arya: update the ads_conn cap?
    pd_data->pd.init_data->ads_conn.id = ads_data->ads.id;
    client_data->cpu.binded_ads_id = ads_data->ads.id;

err_goto:
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_CHANGE_VSPACE_ACK_END);
    return tag;
}

static seL4_MessageInfo_t cpu_component_handle(seL4_MessageInfo_t tag,
                                               seL4_Word sender_badge,
                                               seL4_CPtr received_cap,
                                               bool *need_new_recv_cap)
{
    enum cpu_component_funcs func = seL4_GetMR(CPUMSGREG_FUNC);
    seL4_MessageInfo_t reply_tag;

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        reply_tag = handle_cpu_allocation(sender_badge);
    }
    else
    {
        switch (func)
        {
        case CPU_FUNC_START_REQ:
            reply_tag = handle_start_req(sender_badge, tag);
            break;

        case CPU_FUNC_CONFIG_REQ:
            reply_tag = handle_config_req(sender_badge, tag);
            break;
        case CPU_FUNC_CHANGE_VSPACE_REQ:
            reply_tag = handle_change_vspace_req(sender_badge, tag, received_cap);
            *need_new_recv_cap = true;
            break;
        default:
            gpi_panic(CPUSERVS "Unknown func type.", (seL4_Word)func);
            break;
        }
    }

    return reply_tag;
}

int cpu_component_initialize(simple_t *server_simple,
                             vka_t *server_vka,
                             seL4_CPtr server_cspace,
                             vspace_t *server_vspace,
                             sel4utils_thread_t server_thread,
                             vka_object_t server_ep_obj)
{
    int error = 0;

    // Create the default CPU resource space
    resspc_component_registry_entry_t *space_entry;

    resspc_config_t resspc_config = {
        .type = GPICAP_TYPE_CPU,
        .ep = get_gpi_server()->server_ep_obj.cptr,
    };

    error = resource_component_allocate(get_resspc_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL, false, (void *)&resspc_config,
                                        (resource_server_registry_node_t **)&space_entry, NULL);
    assert(error == 0);

    // Initialize the component
    resource_component_initialize(get_cpu_component(),
                                  GPICAP_TYPE_CPU,
                                  space_entry->space.id,
                                  cpu_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))cpu_new,
                                  on_cpu_registry_delete,
                                  sizeof(cpu_component_registry_entry_t),
                                  server_simple,
                                  server_vka,
                                  server_cspace,
                                  server_vspace,
                                  server_thread,
                                  server_ep_obj.cptr);
}

/** --- Functions callable by root task --- **/

int forge_cpu_cap_from_tcb(sel4utils_process_t *process, // Change this to the sel4utils_thread_t
                           vka_t *vka, uint32_t client_id,
                           seL4_CPtr *cap_ret, uint32_t *id_ret)
{
    OSDB_PRINTF("Forging CPU cap from TCB\n");

    assert(process != NULL);

    int error = 0;
    seL4_CPtr ret_cap;
    cpu_component_registry_entry_t *new_entry;

    /* Allocate the CPU object */
    error = resource_component_allocate(get_cpu_component(), client_id, BADGE_OBJ_ID_NULL, false, NULL,
                                        (resource_server_registry_node_t **)&new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new CPU object for forge\n");

    /* Update the CPU object from TCB */
    new_entry->cpu.thread = process->thread;
    // client_reg_ptr->cpu.tls_base = &process->thread.tls_base;
    new_entry->cpu.cspace = process->cspace.cptr;

    *cap_ret = ret_cap;

    if (id_ret)
    {
        *id_ret = new_entry->cpu.id;
    }

err_goto:
    return error;
}