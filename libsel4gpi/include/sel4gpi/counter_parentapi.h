/**
 * @file counter_parentapi.h
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief API for a parent to spawn a counter server.
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/process.h>

/** @file API for allowing a thread to act as the parent to a serial server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

/** Spawns the server thread. Server thread is spawned within the VSpace and
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
 * @return seL4_Error value.
 */
seL4_Error counter_server_parent_spawn_thread(simple_t *parent_simple,
                                             vka_t *parent_vka,
                                             vspace_t *parent_vspace,
                                             uint8_t priority);