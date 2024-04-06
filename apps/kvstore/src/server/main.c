/**
 * @file Runs the kvstore server in its own process
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <sel4gpi/pd_utils.h>

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* Initialization for static morecore */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K * 100)
char *morecore_area = (char *) PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t) PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t) (PD_HEAP_LOC + APP_MALLOC_SIZE);

#include <sel4gpi/pd_clientapi.h>
#include <fs_client.h>
#include <kvstore_shared.h>
#include <kvstore_server.h>

#define CHECK_ERROR(error, msg)    \
    do                             \
    {                              \
        if (error != seL4_NoError) \
        {                          \
            ZF_LOGE("%s %s: %s"    \
                    ", %d.",       \
                    KVSTORE_S,     \
                    __func__,      \
                    msg,           \
                    error);        \
            goto main_exit;        \
        }                          \
    } while (0);

static seL4_CPtr mcs_reply;

static seL4_MessageInfo_t recv(seL4_CPtr ep, seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(ep,
                    sender_badge_ptr,
                    mcs_reply);
}

static void reply(seL4_MessageInfo_t tag)
{
    api_reply(mcs_reply, tag);
}

int main(int argc, char **argv)
{
    printf("kvstore main!\n");
    
    int error;
    seL4_MessageInfo_t tag;
    seL4_CPtr badge;

    /* parse args */
    assert(argc == 1);
    seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
    seL4_CPtr fs_ep = sel4gpi_get_rde(GPICAP_TYPE_FILE);
    
    printf("kvstore-server: parent ep (%d), fs ep(%d) \n", (int)parent_ep, (int)fs_ep);

    pd_client_context_t pd_conn;
    pd_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    /* initialize server */
    error = kvstore_server_init();
    CHECK_ERROR(error, "failed to initialize kvstore server\n");

    /* allocate our own endpoint */
    seL4_CPtr ep;
    error = pd_client_alloc_ep(&pd_conn, &ep);
    CHECK_ERROR(error, "failed to allocate endpoint\n");

    /* notify parent that we have started */
    KVSTORE_PRINTF("Messaging parent process at slot %d, sending ep (%d)\n", (int)parent_ep, (int)ep);
    tag = seL4_MessageInfo_new(0, 0, 1, 0);
    seL4_SetCap(0, ep);
    seL4_Send(parent_ep, tag);

    /* start serving requests */
    while (1)
    {
        /* Receive a message */
        KVSTORE_PRINTF("Ready to receive a message\n");
        tag = recv(ep, &badge);
        int op = seL4_GetMR(KVMSGREG_FUNC);
        KVSTORE_PRINTF("Received message\n");

        if (op == KV_FUNC_SET_REQ)
        {
            seL4_Word key = seL4_GetMR(KVMSGREG_SET_REQ_KEY);
            seL4_Word val = seL4_GetMR(KVMSGREG_SET_REQ_VAL);

            error = kvstore_server_set(key, val);

            // Restore state of message registers for reply
            seL4_MessageInfo_ptr_set_length(&tag, KVMSGREG_SET_ACK_END);
            seL4_MessageInfo_ptr_set_label(&tag, error);
            seL4_SetMR(KVMSGREG_FUNC, KV_FUNC_SET_ACK);
        }
        else if (op == KV_FUNC_GET_REQ)
        {
            seL4_Word key = seL4_GetMR(KVMSGREG_GET_REQ_KEY);
            seL4_Word val;

            error = kvstore_server_get(key, &val);

            // Restore state of message registers for reply
            seL4_MessageInfo_ptr_set_length(&tag, KVMSGREG_GET_ACK_END);
            seL4_MessageInfo_ptr_set_label(&tag, error);
            seL4_SetMR(KVMSGREG_GET_ACK_VAL, val);
            seL4_SetMR(KVMSGREG_FUNC, KV_FUNC_GET_ACK);
        }
        else
        {
            KVSTORE_PRINTF("Got invalid opcode (%d)\n", op);
        }

        /* Reply to message */
        reply(tag);
    }

main_exit:
    /* notify parent that we have failed */
    KVSTORE_PRINTF("Messaging parent process at slot %d, notifying of failure\n", (int)parent_ep);
    tag = seL4_MessageInfo_new(error, 0, 0, 0);
    seL4_Send(parent_ep, tag);
}