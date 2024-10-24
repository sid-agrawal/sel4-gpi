/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <autoconf.h>
#include <sel4utils/gen_config.h>

#include <vka/vka.h>

#include <vspace/vspace.h>
#include <elf/elf.h>
#include <sel4gpi/ads_component.h>

/**
 * @param
 * Elf loading utils for GPI, modified from sel4utils/elf
 */

#if CONFIG_WORD_SIZE == 64
#define Elf_Phdr Elf64_Phdr
#elif CONFIG_WORD_SIZE == 32
#define Elf_Phdr Elf32_Phdr
#else
#error "Word size unsupported"
#endif /* CONFIG_WORD_SIZE */

typedef struct sel4gpi_elf_region
{
    seL4_CapRights_t rights;
    /* These two vstarts may differ if the elf was not mapped 1to1. Such an elf is not
     * runnable, but allows it to be loaded into a vspace where it is not intended to be run.
     * This is also as reported by the elf file. */
    void *elf_vstart;
    /* Start of the reservation. This will be 4k aligned */
    void *reservation_vstart;
    /* Size of the elf segment as reported by elf file */
    uint32_t size;
    /* Size of the reservation.  This will be a multple of 4k */
    size_t reservation_size;
    uint32_t reservation_pages;
    attach_node_t *reservation;
    int cacheable;
    /* Index of this elf segment in the section header */
    int segment_index;
    /* whether the region is executable, this is a separate field because it is not captured by seL4_CapRights_t*/
    bool executable;
    /* the address of the region in the source */
    const char *src;
    size_t src_size;
} sel4gpi_elf_region_t;

/**
 * Load an elf file into an ads.
 *
 * The loader ads and vka allocation will be preserved (no extra cslots or objects or vaddrs
 * will leak from this function), even in the case of an error.
 *
 * The loadee ads and vka will alter: cslots will be allocated for each frame to be
 * mapped into the address space and frames will be allocated. In case of failure the entire
 * virtual address space is left in the state where it failed.
 * Returns the list of vspace reservations made during loads - primarily for OSmosis tracking purposes.
 *
 * @param loadee the ads to load the elf file into
 * @param loader the ads we are loading from
 * @param loadee_vka allocator to use for allocation in the loadee vspace
 * @param loader_vka allocator to use for loader vspace. Can be the same as loadee_vka.
 * @param elf the elf file to load
 *
 * @return The entry point of the new process, NULL on error
 */
void *
sel4gpi_elf_load(ads_t *loadee, ads_t *loader, vka_t *loadee_vka,
                 vka_t *loader_vka, const elf_t *elf);

/**
 * Parses an elf file and returns the number of loadable regions. The result of this
 * is used to calculate the number of regions to pass to sel4gpi_elf_reserve and
 * sel4gpi_elf_load_record_regions
 *
 * @param image_name name of the image in the cpio archive to inspect
 * @return Number of loadable regions in the elf
 */
int sel4gpi_elf_num_regions(const elf_t *elf);

/**
 * Looks for the __vsyscall section in an elf file and returns the value. This
 * is used to set the __sysinfo value when launching the elf
 *
 * @param image_name name of the image in the cpio archive to inspect
 *
 * @return Address of vsyscall function or 0 if not found
 */
uintptr_t sel4gpi_elf_get_vsyscall(const elf_t *elf);

/**
 * Finds the section_name section in an elf file and returns the address.
 *
 * @param image_name name of the image in the cpio archive to inspect
 *
 * @param section_name name of the section to find
 *
 * @param section_size optional pointer to uint64_t to return the section size
 *
 * @return Address of section or 0 if not found
 */
uintptr_t sel4gpi_elf_get_section(const elf_t *elf, const char *section_name, uint64_t *section_size);

/**
 * Parses an elf file and returns the number of phdrs. The result of this
 * can be used prior to a call to sel4gpi_elf_read_phdrs
 *
 * @param image_name name of the image in the cpio archive to inspect
 * @return Number of phdrs in the elf
 */
uint32_t sel4gpi_elf_num_phdrs(const elf_t *elf);

/**
 * Parse an elf file and retrieve all the phdrs
 *
 * @param image_name name of the image in the cpio archive to inspect
 * @param max_phdrs Maximum number of phdrs to retrieve
 * @param phdrs Array to store the loaded phdrs into
 *
 * @return Number of phdrs retrieved
 */
void sel4gpi_elf_read_phdrs(const elf_t *elf, size_t max_phdrs, Elf_Phdr *phdrs);
