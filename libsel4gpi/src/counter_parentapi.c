/**
 * @file counter_parentapi.c    
 * @author Sid Agrawal(sid@sid-agrawal.c)
 * @brief Implements functions needed by a parent to interact with the counter server from counter_parentapi.h
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sel4/sel4.h>
#include <sel4utils/strerror.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vka/object_capops.h>

#include <sel4gpi/counter_parentapi.h>

seL4_Error
serial_server_parent_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                                  vspace_t *parent_vspace,
                                  uint8_t priority)
                                  {
                                      return 0;
                                  }