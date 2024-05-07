#pragma once
#include <sel4/sel4.h>
#include <stdint.h>
#include <utils/page.h>

/* (XXX) Arya: temp definition of supported images */
#define PD_N_IMAGES 6
static const char *pd_images[PD_N_IMAGES] = {"hello", "hello_kvstore", "ramdisk_server", "fs_server", "kvstore_server", "hello_benchmark"};
static const uint64_t pd_image_heap_size[PD_N_IMAGES] = {PAGE_SIZE_4K, 100 * PAGE_SIZE_4K, 100 * PAGE_SIZE_4K, 100 * PAGE_SIZE_4K, 100 * PAGE_SIZE_4K, PAGE_SIZE_4K};

#define PD_MAX_ARGC 4

int sel4gpi_image_name_to_id(const char *image_name);
