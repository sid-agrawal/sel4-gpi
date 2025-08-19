/**
 * @file API for allowing a thread to act as the parent to a sddf blk server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>

#define VIRTIO_REGS_PADDR 0xa003000
#define VIRTIO_REGS_PAGES 1
#define VIRTIO_HEADERS_PADDR 0x5fff0000
#define VIRTIO_HEADERS_PAGES 16
#define VIRTIO_METADATA_PADDR 0x5fdf0000
#define VIRTIO_METADATA_PAGES 512
#define IRQ_NUMBER 79

#define VIRT_IRQ 0x1
#define VIRT_NOTIFICATION 0x2
#define BADGE_V2C 0x11
#define BADGE_C2V 0x22
#define BADGE_D2V 0x44
#define BADGE_V2D 0x88

#define DRIVER_STORAGE_INFO_PAGES 1
#define DRIVER_REQUEST_PAGES 512
#define DRIVER_RESPONSE_PAGES 512
#define DRIVER_DATA_PAGES 512
#define DRIVER_DATA_PADDR 0x5fbf0000

#define SDDF_BLK_SERVER_DEFAULT_PRIORITY 199
#define MAX_CLIENT_ID 64

/* Context of the server */

typedef struct _sddf_blk_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

} sddf_blk_server_context_t;

/**
 * To be run once at the start of the sddf_blk server
 */
int sddf_blk_init();

/**
 * To handle client requests to the sddf_blk server
 */
void sddf_blk_request_handler(void *msg_p,
                             void *msg_reply_p,
                             seL4_Word sender_badge,
                             seL4_CPtr cap,
                             bool *need_new_recv_cap);

/**
 * To handle root task requests to the ramdisk server
 */
int sddf_blk_work_handler(PdWorkReturnMessage *work);

sddf_blk_server_context_t *get_sddf_blk_server(void);