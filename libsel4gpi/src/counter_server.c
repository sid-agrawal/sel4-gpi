/**
 * @file counter_server.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the counter server API from counter_server.h.
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

#include <sel4gpi/counter_clientapi.h>
#include <sel4gpi/counter_parentapi.h>
#include <sel4gpi/counter_server.h>


static counter_server_context_t counter_server;

counter_server_context_t *get_counter_server(void)
{
    return &counter_server;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_counter_server()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_counter_server()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_counter_server()->server_thread.reply.cptr, tag);
}


/**
 * @brief Insert a new client into the client registry Linked List.
 * 
 * @param new_node 
 */
static void counter_server_registry_insert(counter_server_registry_entry_t *new_node) {

    counter_server_registry_entry_t *head = get_counter_server()->client_registry;

    if (head == NULL) {
        get_counter_server()->client_registry = new_node;
        new_node->next = NULL;
        return;
    }

    while (head->next != NULL) {
        head = head->next;
    }
    head->next = new_node;
    new_node->next = NULL;
}

/**
 * @brief Lookup the client registry entry for the give badge.
 * 
 * @param badge 
 * @return counter_server_registry_entry_t* 
 */
static counter_server_registry_entry_t *counter_server_registry_get_entry_by_badge(seL4_Word badge){

    counter_server_registry_entry_t *current_ctx = get_counter_server()->client_registry;

    while (current_ctx != NULL) {
        if ((seL4_Word)current_ctx == badge) {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

/**
 * @brief The starting point for the counter server's thread.
 *
 */
void counter_server_main()
{
    seL4_MessageInfo_t tag;
    enum counter_server_funcs func;
    seL4_Error error = 0;
    counter_server_registry_entry_t *client_data = NULL;
    size_t buff_len, bytes_written;

    /* The Parent will seL4_Call() to us, the Server, right after spawning us.
     * It will expect us to seL4_Reply() with an error status code - we will
     * send this Reply.
     *
     * First call seL4_Recv() to get the Reply cap back to the Parent, and then
     * seL4_Reply to report our status.
     */
    seL4_Word sender_badge;
    recv(&sender_badge);
    assert(sender_badge == COUNTER_SERVER_BADGE_PARENT_VALUE);

    seL4_SetMR(CSMSGREG_FUNC, FUNC_SERVER_SPAWN_SYNC_ACK);
    tag = seL4_MessageInfo_new(error, 0, 0, CSMSGREG_SPAWN_SYNC_ACK_END);
    reply(tag);

    /* If the bind failed, this thread has essentially failed its mandate, so
     * there is no reason to leave it scheduled. Kill it (to whatever extent
     * that is possible).
     */

    printf(COUNTERSERVS"counter_server_main: Got a call from the parent.\n");
    if (error != 0)
    {
        seL4_TCB_Suspend(get_serial_server()->server_thread.tcb.cptr);
    }


    printf(COUNTERSERVS"main: Entering main loop and accepting requests.\n");
    while (1) {
        tag = recv(&sender_badge);
        printf(COUNTERSERVS "main: Got message from %x\n", sender_badge);

        func = seL4_GetMR(CSMSGREG_FUNC);

        // if the badge is not set, then it has to be a new connection request.
        if (sender_badge == COUNTER_SERVER_BADGE_VALUE_EMPTY && func != FUNC_CONNECT_REQ){
            printf(COUNTERSERVS "main: Badge not set, but not a connect request.\n");
            continue;
        }

        switch (func) {
        case FUNC_CONNECT_REQ:
            printf(COUNTERSERVS"main: Got connect request from client badge %x.\n",
                    sender_badge);

            /* Allocate a new registry entry for the client. */
            seL4_Word client_reg_ptr = (seL4_Word) malloc(sizeof(counter_server_registry_entry_t));
            if (client_reg_ptr == 0) {
                printf(COUNTERSERVS "main: Failed to allocate new badge for client.\n");
                continue;
            }
            memset((void *) client_reg_ptr, 0, sizeof(counter_server_registry_entry_t));
            counter_server_registry_insert((counter_server_registry_entry_t *) client_reg_ptr);

            /* Create a badged endpoint for the client to send messages to.
             * Use the address of the client_registry_entry as the badge.
             */ 
            cspacepath_t src, dest;
            vka_cspace_make_path(get_counter_server()->server_vka,
                                get_counter_server()->server_ep_obj.cptr, &src);
            seL4_CPtr dest_cptr;
            vka_cspace_alloc(get_counter_server()->server_vka, &dest_cptr);
            vka_cspace_make_path(get_counter_server()->server_vka, dest_cptr, &dest);

            error = vka_cnode_mint(&dest, &src, seL4_AllRights, client_reg_ptr);
            if (error) {
                printf(COUNTERSERVS "main: Failed to mint client badge %x.\n",
                        client_reg_ptr);
                continue;
            }
            /* Return this badged end point in the return message. */
            seL4_SetCap(0, dest.capPtr);
            seL4_SetMR(CSMSGREG_FUNC, FUNC_CONNECT_ACK);
            tag = seL4_MessageInfo_new(error, 0, 1, CSMSGREG_CONNECT_ACK_END);
            reply(tag);
            break;

        case FUNC_INCREMENT_REQ:
            printf(COUNTERSERVS"main: Got increment request from client badge %x.\n",
                    sender_badge);

            /* Find the client */
            client_data = counter_server_registry_get_entry_by_badge(sender_badge);
            if (client_data == NULL) {
                printf(COUNTERSERVS "main: Failed to find client badge %x.\n",
                        sender_badge);
                continue;
            }
            printf(COUNTERSERVS"main: found client_data %x.\n", client_data);
            printf(COUNTERSERVS"main: old counter value %d.\n", client_data->counter);

            counter_increment(&client_data->counter);

            printf(COUNTERSERVS"main: new counter value %d.\n", client_data->counter);
            seL4_SetMR(CSMSGREG_FUNC, FUNC_INCREMENT_ACK);
            tag = seL4_MessageInfo_new(error, 0, 0, CSMSGREG_INCREMENT_ACK_END);
            reply(tag);
            break;


        default:
            ZF_LOGW(COUNTERSERVS "main: Unknown function %d requested.", func);
            break;
        }
    }

    //serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
    ZF_LOGI(COUNTERSERVS"main: Suspending.");
    seL4_TCB_Suspend(get_serial_server()->server_thread.tcb.cptr);
}