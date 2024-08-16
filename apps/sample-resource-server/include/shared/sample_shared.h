#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>

/* Sample resource server configuration */

#define SAMPLE_DEBUG 1
#define SAMPLE_RPC_MAGIC 0x53414D50 // it's SAMP in ascii code
#define SAMPLE_RESOURCE_TYPE_NAME "SAMPLE"

//  Include other configuration options here