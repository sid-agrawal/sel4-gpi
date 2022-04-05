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
 * @brief The starting point for the counter server's thread.
 * 
 */
void counter_server_main()  {
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
    assert(sender_badge == COUNTER_SERVER_BADGE_DEFAULT_VALUE);
    

    seL4_SetMR(CSMSGREG_FUNC, FUNC_SERVER_SPAWN_SYNC_ACK);
    tag = seL4_MessageInfo_new(error, 0, 0, CSMSGREG_SPAWN_SYNC_ACK_END);
    reply(tag);

    /* If the bind failed, this thread has essentially failed its mandate, so
     * there is no reason to leave it scheduled. Kill it (to whatever extent
     * that is possible).
     */

    printf("counter_server_main: Got a call from the parent.");
    if (error != 0) {
        seL4_TCB_Suspend(get_serial_server()->server_thread.tcb.cptr);
    }
}