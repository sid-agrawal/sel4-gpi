#pragma once
#include <sel4/sel4.h>
#include <stdint.h>
#include <utils/page.h>
#include <vka/vka.h>

/* (XXX) Arya: temp definition of supported images */
#define PD_N_IMAGES 6
static const char *pd_images[PD_N_IMAGES] = {"hello", "hello_kvstore", "ramdisk_server", "fs_server", "kvstore_server", "hello_benchmark"};

#define PD_MAX_ARGC 4

typedef struct _cpu_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
} cpu_client_context_t;

typedef struct _ads_client_context
{
   cspacepath_t badged_server_ep_cspath;
   uint64_t id;
   // cspacepath_t public_server_ep_cspath;
} ads_client_context_t;

typedef struct _pd_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
} pd_client_context_t;

typedef struct _mo_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
   uint64_t id; // Needed only for RR dump
} mo_client_context_t;

int sel4gpi_image_name_to_id(const char *image_name);
