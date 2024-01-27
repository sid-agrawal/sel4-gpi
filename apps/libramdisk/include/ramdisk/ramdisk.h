/**
 * @file ramdisk.h
 * @author 
 * @brief Implements functions needed by a parent to interact with the ramdisk server.
 * @version 0.1
 * @date 2024-01-25
 */

#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

#define RAMDISK_S "RamDisk Server: "
#define RAMDISK_SERVER_DEFAULT_PRIORITY    (seL4_MaxPrio - 100)

/* ramdisk operations */
#define RAMDISK_read	0
#define RAMDISK_write	1
#define RAMDISK_flush	2

/* ramdisk configuration */
#define RAMDISK_SIZE_BITS 21
#define RAMDISK_SIZE_BYTES SIZE_BITS_TO_BYTES(RAMDISK_SIZE_BITS)


/** @file API for allowing a thread to act as the parent to a ramdisk server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

/** Spawns the ramdisk server thread. Server thread is spawned within the VSpace and
 *  CSpace of the thread that spawned it.
 *
 * CAUTION:
 * All vka_t, vpsace_t, and simple_t instances passed to this library by
 * reference must remain functional throughout the lifetime of the server.
 *
 * @param parent_simple Initialized simple_t for the parent process that is
 *                      spawning the server thread.
 * @param parent_vka Initialized vka_t for the parent process that is spawning
 *                   the server thread.
 * @param parent_vspace Initialized vspace_t for the parent process that is
 *                      spawning the server thread.
 * @param priority Server thread's priority.
 * @param server_endpoint Server thread's endpoint cap.
 * @return seL4_Error value.
 */
seL4_Error
ramdisk_server_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                            vspace_t *parent_vspace,
                            uint8_t priority,
                            seL4_CPtr *server_ep_cap);

/*
Context of the server
*/
typedef struct _ramdisk_server_context {
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint.
    vka_object_t server_ep_obj;

    // Memory for ramdisk
    void *ramdisk_buf;
    vka_object_t ramdisk_buf_obj;
} ramdisk_server_context_t;

/**
 * Internal library function: acts as the main() for the server thread.
 **/
void ramdisk_server_main(void);

ramdisk_server_context_t *get_ramdisk_server(void);