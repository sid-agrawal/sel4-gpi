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
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/gpi_rpc.h>
#include <cpu_component_rpc.pb.h>
#include <sel4debug/register_dump.h>

// Defined for utility printing macros
#define DEBUG_ID CPU_DEBUG
#define SERVER_ID CPUSERVS
#define DEFAULT_ERR CpuComponentError_UNKNOWN

resource_component_context_t *get_cpu_component(void)
{
    return &get_gpi_server()->cpu_component;
}

// Called when an item from the CPU registry is deleted
static void on_cpu_registry_delete(resource_registry_node_t *node_gen, void *arg)
{
    cpu_component_registry_entry_t *node = (cpu_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying CPU (%u)\n", node->cpu.id);

    resource_component_remove_from_rt(get_cpu_component(), node->cpu.id);

    cpu_destroy(&node->cpu);
}

int cpu_component_allocate(gpi_obj_id_t client_id, cpu_t **ret_cpu, seL4_CPtr *ret_cap)
{
    int error = 0;
    cpu_component_registry_entry_t *new_entry;

    /* Create the CPU object */
    error = resource_component_allocate(get_cpu_component(), client_id, BADGE_OBJ_ID_NULL, false, NULL,
                                        (resource_registry_node_t **)&new_entry, ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new CPU object\n");

    *ret_cpu = &new_entry->cpu;

err_goto:
    return error;
}

static void handle_cpu_allocation(seL4_Word sender_badge, CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got CPU allocation request from %lx\n", sender_badge);
    BADGE_PRINT(sender_badge);

    int error = 0;
    seL4_CPtr ret_cap;
    cpu_t *cpu;
    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);

    error = cpu_component_allocate(client_id, &cpu, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate new CPU object\n");

    reply_msg->msg.alloc.slot = ret_cap;
    reply_msg->msg.alloc.id = cpu->id;

err_goto:
    reply_msg->which_msg = CpuReturnMessage_alloc_tag;
    reply_msg->errorCode = error;
}

static void handle_start_req(seL4_Word sender_badge, CpuStartMessage *msg, CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got CPU start req: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    /* Find the client */
    cpu_component_registry_entry_t *client_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find CPU (%u)\n", get_object_id_from_badge(sender_badge));

    error = cpu_start(&client_data->cpu);
    SERVER_GOTO_IF_ERR(error, "Failed to start CPU\n");

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

int cpu_component_configure(cpu_t *cpu,
                            ads_t *ads,
                            pd_t *pd,
                            uint64_t cnode_guard,
                            seL4_CPtr fault_ep,
                            mo_t *ipc_buf_mo,
                            void *ipc_buf_addr,
                            int prio)
{
    int error = 0;

    seL4_CNode cspace_root = pd->cspace.cptr;

    /* Get the frame from the IPC buf MO */
    seL4_CPtr ipc_buf_frame = ipc_buf_mo == NULL ? seL4_CapNull : ipc_buf_mo->frame_caps_in_root_task[0];

    /* Update the IPC buf refcount if it's being replaced with a new IPC buffer */
    if (cpu->ipc_buf_mo)
    {
        error = resource_component_dec(get_mo_component(), cpu->ipc_buf_mo);
        SERVER_GOTO_IF_ERR(error, "Failed to decrement refcount of old IPC buf mo (%u)\n", cpu->ipc_buf_mo);
        cpu->ipc_buf_mo = 0;
    }

    if (ipc_buf_mo)
    {
        error = resource_component_inc(get_mo_component(), ipc_buf_mo->id);
        SERVER_GOTO_IF_ERR(error, "Failed to increment refcount of IPC buf mo (%u)\n", ipc_buf_mo->id);
        cpu->ipc_buf_mo = ipc_buf_mo->id;
    }

    if (cpu->binded_ads_id)
    {
        error = resource_component_dec(get_ads_component(), cpu->binded_ads_id);
        SERVER_GOTO_IF_ERR(error, "Failed to decrement refcount of old ADS (%u)\n", cpu->binded_ads_id);
        cpu->binded_ads_id = 0;
    }

    resource_component_inc(get_ads_component(), ads->id);

    /* Configure the vspace */
    error = cpu_config_vspace(cpu,
                              ads->vspace,
                              cspace_root,
                              (uint64_t)cnode_guard,
                              fault_ep,
                              ipc_buf_frame,
                              ipc_buf_addr,
                              prio);

    /* Set the bound notification */
    error = cpu_bind_notif(cpu, pd->notification.cptr);
    SERVER_GOTO_IF_ERR(error, "Failed to configure vspace for CPU (%u)\n", cpu->id);

    cpu->binded_ads_id = ads->id;
    OSDB_PRINTF("Finished configuring CPU\n");

err_goto:
    return error;
}

static void handle_config_req(seL4_Word sender_badge,
                              CpuConfigMessage *msg, CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got CPU config request from client badge %lx\n", sender_badge);

    int error = 0;
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_caps_2(GPICAP_TYPE_PD, GPICAP_TYPE_ADS),
                        "Did not receive PD/ADS caps\n");

    /* Find the CPU */
    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND(cpu_data == NULL, "Couldn't find CPU (%u)\n", get_object_id_from_badge(sender_badge));

    pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_badge(seL4_GetBadge(0));
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%u)\n", get_object_id_from_badge(seL4_GetBadge(0)));

    /* Find the ADS */
    seL4_Word ads_cap_badge = seL4_GetBadge(1);
    ads_component_registry_entry_t *ads_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), ads_cap_badge);
    SERVER_GOTO_IF_COND(ads_data == NULL, "Couldn't find ADS (%u)\n", get_object_id_from_badge(ads_cap_badge));

    /* Find the IPC MO, if it exists (OK if it doesn't exist) */
    seL4_Word ipc_buf_mo_badge = seL4_GetBadge(2);
    mo_component_registry_entry_t *ipc_mo_data = (mo_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_mo_component(), ipc_buf_mo_badge);

    /* Do the configurataion */
    error = cpu_component_configure(
        &cpu_data->cpu,
        &ads_data->ads,
        &pd_data->pd,
        (uint64_t)msg->cnode_guard,
        msg->fault_ep_cap,
        ipc_mo_data == NULL ? NULL : &ipc_mo_data->mo,
        (void *)msg->ipc_buf_addr,
        msg->prio);

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_change_vspace_req(seL4_Word sender_badge,
                                     CpuChangeVspaceMessage *msg, CpuReturnMessage *reply_msg,
                                     seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got change vspace from client badge %lx\n", sender_badge);

    int error = 0;
    SERVER_GOTO_IF_COND(!sel4gpi_rpc_check_cap(GPICAP_TYPE_ADS), "Did not receive ADS cap\n");

    /* Find the client */
    cpu_component_registry_entry_t *client_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find CPU (%u)\n", get_object_id_from_badge(sender_badge));

    /* Find the PD */
    pd_component_registry_entry_t *pd_data =
        pd_component_registry_get_entry_by_id(get_client_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%u)\n", get_client_id_from_badge(sender_badge));

    /* Find the ADS */
    seL4_Word ads_cap_badge = seL4_GetBadge(0);
    ads_component_registry_entry_t *ads_data = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_ads_component(), ads_cap_badge);
    SERVER_GOTO_IF_COND(ads_data == NULL, "Couldn't find ADS (%u)\n", get_object_id_from_badge(ads_cap_badge));
    vspace_t *ads_vspace = ads_data->ads.vspace;

    // Change vspace
    error = cpu_change_vspace(&client_data->cpu,
                              get_cpu_component()->server_vka,
                              ads_vspace);

    SERVER_GOTO_IF_ERR(error, "Failed to change vspace\n");

    // Update refcount of the ADS objects
    uint64_t old_ads_id = pd_data->pd.shared_data->ads_conn.id;
    resource_component_dec(get_ads_component(), old_ads_id);
    resource_component_inc(get_ads_component(), ads_data->ads.id);

    // Update the PD object with the new ADS
    // (XXX) Arya: update the ads_conn cap? need to find the badged EP for the given ADS
    pd_data->pd.shared_data->ads_conn.id = ads_data->ads.id;
    client_data->cpu.binded_ads_id = ads_data->ads.id;

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_set_tls_req(seL4_Word sender_badge, CpuTlsBaseMessage *msg, CpuReturnMessage *reply_msg)
{
    int error = 0;
    OSDB_PRINTF("Got set TLS base request:");
    BADGE_PRINT(sender_badge);

    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU data\n");
    void *tls_base = (void *)msg->tls_base_addr;

    error = cpu_set_tls_base(&cpu_data->cpu, tls_base, false);
    SERVER_GOTO_IF_ERR(error, "Failed to set TLS \n");

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_elevate_req(seL4_Word sender_badge, CpuElevatePrivilegeMessage *msg, CpuReturnMessage *reply_msg)
{
    int error = 0;
    OSDB_PRINTF("Got elevate CPU request: ");
    BADGE_PRINT(sender_badge);

    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU data\n");

    error = cpu_elevate(&cpu_data->cpu);
    SERVER_GOTO_IF_ERR(error, "Failed to set TLS \n");

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_read_registers_req(seL4_Word sender_badge, CpuReadRegistersMessage *msg, CpuReturnMessage *reply_msg)
{
    int error = 0;
    OSDB_PRINTF("Got read registers request: ");
    BADGE_PRINT(sender_badge);

    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU data\n");

    error = cpu_read_registers(&cpu_data->cpu, (seL4_UserContext *)reply_msg->msg.read_reg.reg_buf);
    if (!error)
    {
        reply_msg->msg.read_reg.reg_buf_count = SEL4_USER_CONTEXT_COUNT;
        // sel4debug_print_registers((seL4_UserContext *)reply_msg->msg.read_reg.reg_buf);
    }

err_goto:
    reply_msg->which_msg = CpuReturnMessage_read_reg_tag;
    reply_msg->errorCode = error;
}

static void handle_suspend_req(seL4_Word sender_badge, CpuReturnMessage *reply_msg, bool *should_reply)
{
    int error = 0;
    OSDB_PRINTF("Got suspend request: ");
    BADGE_PRINT(sender_badge);

    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU from badge: ");

    error = cpu_component_stop(get_object_id_from_badge(sender_badge));

    /* the PD whose running on this CPU requested to be suspended, so it will not receive a reply */
    if (cpu_data->cpu.id == get_client_id_from_badge(sender_badge))
    {
        *should_reply = false;
        return;
    }

err_goto:
    *should_reply = true;
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_disconnect_req(seL4_Word sender_badge,
                                  CpuDisconnectMessage *msg,
                                  CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got disconnect request from Client: ");
    BADGE_PRINT(sender_badge);

    int error = 0;

    gpi_obj_id_t cpu_id = get_object_id_from_badge(sender_badge);

    /* Find the PD */
    pd_component_registry_entry_t *pd_data =
        pd_component_registry_get_entry_by_id(get_client_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(pd_data == NULL, "Couldn't find PD (%u)\n", get_client_id_from_badge(sender_badge));

    /* Remove the MO from the client CPU */
    error = pd_remove_resource(&pd_data->pd, make_res_id(GPICAP_TYPE_CPU, get_cpu_component()->space_id, cpu_id));
    SERVER_GOTO_IF_ERR(error, "Failed to remove MO from PD\n");

    // This will reduce the refcount of the CPU, and then it will be deleted if necessary

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_write_reg_req(seL4_Word sender_badge, CpuWriteRegistersMessage *msg, CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got write registers request from Client: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU from badge: ");

    error = cpu_write_registers(&cpu_data->cpu, (seL4_UserContext *)msg->reg_buf, msg->reg_buf_count, msg->resume);

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_inject_irq_req(seL4_Word sender_badge, CpuInjectIrqMessage *msg, CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got 'inject IRQ' request from Client: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU from badge: ");
    if (cpu_data->cpu.vcpu.cptr != seL4_CapNull)
    {
        error = cpu_inject_irq(&cpu_data->cpu, msg->virq, msg->prio, msg->group, msg->idx);
    }

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_ack_vppi_req(seL4_Word sender_badge, CpuAckVppiMessage *msg, CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got 'inject IRQ' request from Client: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU from badge: ");
    if (cpu_data->cpu.vcpu.cptr != seL4_CapNull)
    {
        error = cpu_ack_vppi(&cpu_data->cpu, msg->irq);
    }

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void handle_read_vcpu_req(seL4_Word sender_badge, CpuReadVcpuMessage *msg, CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got 'read vcpu registers' request from Client: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU from badge: ");

    cpu_read_vcpu_regs(&cpu_data->cpu, (vcpu_regs_t *)reply_msg->msg.read_vcpu.reg_buf);
    reply_msg->msg.read_vcpu.reg_buf_count = SEL4_VCPU_REG_COUNT;

err_goto:
    reply_msg->which_msg = CpuReturnMessage_read_vcpu_tag;
    reply_msg->errorCode = error;
}

static void handle_resume_req(seL4_Word sender_badge, CpuResumeMessage *msg, CpuReturnMessage *reply_msg)
{
    OSDB_PRINTF("Got 'resume' request from Client: ");
    BADGE_PRINT(sender_badge);

    int error = 0;
    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_badge(get_cpu_component(), sender_badge);
    SERVER_GOTO_IF_COND_BG(cpu_data == NULL, sender_badge, "Couldn't find CPU from badge: ");

    error = cpu_resume(&cpu_data->cpu);

err_goto:
    reply_msg->which_msg = CpuReturnMessage_basic_tag;
    reply_msg->errorCode = error;
}

static void cpu_component_handle(void *msg_p,
                                 seL4_Word sender_badge,
                                 seL4_CPtr received_cap,
                                 void *reply_msg_p,
                                 bool *need_new_recv_cap,
                                 bool *should_reply)
{
    int error = 0; // unused, to appease the error handling macros
    CpuMessage *msg = (CpuMessage *)msg_p;
    CpuReturnMessage *reply_msg = (CpuReturnMessage *)reply_msg_p;

    SERVER_GOTO_IF_COND(msg->magic != CPU_RPC_MAGIC,
                        "CPU component received message with incorrect magic number %lx\n", msg->magic);

    if (get_object_id_from_badge(sender_badge) == BADGE_OBJ_ID_NULL)
    {
        SERVER_GOTO_IF_COND(msg->which_msg != CpuMessage_alloc_tag,
                            "Received invalid request on the allocation endpoint\n");
        handle_cpu_allocation(sender_badge, reply_msg);
    }
    else
    {
        switch (msg->which_msg)
        {
        case CpuMessage_start_tag:
            handle_start_req(sender_badge, &msg->msg.start, reply_msg);
            break;
        case CpuMessage_config_tag:
            handle_config_req(sender_badge, &msg->msg.config, reply_msg);
            break;
        case CpuMessage_change_vspace_tag:
            handle_change_vspace_req(sender_badge, &msg->msg.change_vspace, reply_msg, received_cap);
            *need_new_recv_cap = true;
            break;
        case CpuMessage_tls_base_tag:
            handle_set_tls_req(sender_badge, &msg->msg.tls_base, reply_msg);
            break;
        case CpuMessage_elevate_privilege_tag:
            handle_elevate_req(sender_badge, &msg->msg.elevate_privilege, reply_msg);
            break;
        case CpuMessage_write_reg_tag:
            handle_write_reg_req(sender_badge, &msg->msg.write_reg, reply_msg);
            break;
        case CpuMessage_read_reg_tag:
            handle_read_registers_req(sender_badge, &msg->msg.read_reg, reply_msg);
            break;
        case CpuMessage_suspend_tag:
            handle_suspend_req(sender_badge, reply_msg, should_reply);
            break;
        case CpuMessage_disconnect_tag:
            handle_disconnect_req(sender_badge, &msg->msg.disconnect, reply_msg);
            break;
        case CpuMessage_inject_irq_tag:
            handle_inject_irq_req(sender_badge, &msg->msg.inject_irq, reply_msg);
            break;
        case CpuMessage_ack_vppi_tag:
            handle_ack_vppi_req(sender_badge, &msg->msg.ack_vppi, reply_msg);
            break;
        case CpuMessage_read_vcpu_tag:
            handle_read_vcpu_req(sender_badge, &msg->msg.read_vcpu, reply_msg);
            break;
        case CpuMessage_resume_tag:
            handle_resume_req(sender_badge, &msg->msg.resume, reply_msg);
            break;
        default:
            SERVER_GOTO_IF_COND(1, "Unknown request received: %u\n", msg->which_msg);
            break;
        }
    }

    OSDB_PRINTF("Returning from CPU component with error code %u\n", reply_msg->errorCode);
    return;

err_goto:
    OSDB_PRINTF("Returning from CPU component with error code %u\n", error);
    reply_msg->errorCode = error;
}

int cpu_component_initialize(vka_t *server_vka,
                             vspace_t *server_vspace,
                             vka_object_t server_ep_obj)
{
    int error = 0;

    // Create the default CPU resource space
    resspc_component_registry_entry_t *space_entry;

    resspc_config_t resspc_config = {
        .type = GPICAP_TYPE_CPU,
        .ep = get_gpi_server()->server_ep_obj.cptr,
        .pd_id = get_gpi_server()->rt_pd_id,
    };

    error = resource_component_allocate(get_resspc_component(), get_gpi_server()->rt_pd_id, BADGE_OBJ_ID_NULL,
                                        false, (void *)&resspc_config, (resource_registry_node_t **)&space_entry,
                                        NULL);
    assert(error == 0);

    // Initialize the component
    resource_component_initialize(get_cpu_component(),
                                  GPICAP_TYPE_CPU,
                                  space_entry->space.id,
                                  cpu_component_handle,
                                  (int (*)(resource_component_object_t *, vka_t *, vspace_t *, void *))cpu_new,
                                  on_cpu_registry_delete,
                                  sizeof(cpu_component_registry_entry_t),
                                  server_vka,
                                  server_vspace,
                                  server_ep_obj.cptr,
                                  &CpuMessage_msg,
                                  &CpuReturnMessage_msg);
}

/** --- Functions callable by root task --- **/

int cpu_component_stop(gpi_obj_id_t cpu_id)
{
    int error = 0;

    // Find the CPU
    cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_cpu_component(), cpu_id);
    SERVER_GOTO_IF_COND(cpu_data == NULL, "Couldn't find CPU (%u)\n", cpu_id);

    // Stop the CPU
    error = cpu_stop(&cpu_data->cpu);

err_goto:
    return error;
}
