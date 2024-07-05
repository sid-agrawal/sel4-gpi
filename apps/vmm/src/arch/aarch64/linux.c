/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <vmm-common/dtb.h>
#include <vmm-common/linux.h>
#include <assert.h>
#include <string.h>
#include <utils/zf_log.h>

uintptr_t linux_setup_images(uintptr_t ram_start,
                             uintptr_t kernel,
                             size_t kernel_size,
                             uintptr_t dtb_src,
                             uintptr_t dtb_dest,
                             size_t dtb_size,
                             uintptr_t initrd_src,
                             uintptr_t initrd_dest,
                             size_t initrd_size)
{
    // Before doing any copying, let's sanity check the addresses.
    // First we'll check that everything actually lives inside RAM.
    // @ivanv: TODO
    // Check that the kernel and DTB to do not overlap
    uintptr_t dtb_start = dtb_dest;
    uintptr_t dtb_end = dtb_start + dtb_size;
    uintptr_t kernel_start = kernel;
    uintptr_t kernel_end = kernel_start + kernel_size;
    if (!(dtb_start >= kernel_end || dtb_end <= kernel_start))
    {
        ZF_LOGE("Linux kernel image [0x%lx..0x%lx)"
                " overlaps with the destination of the DTB [0x%lx, 0x%lx)\n",
                kernel_start, kernel_end, dtb_start, dtb_end);
        return 0;
    }
    // Check that the kernel and initrd do not overlap
    uintptr_t initrd_start = initrd_dest;
    uintptr_t initrd_end = initrd_start + initrd_size;
    if (!(initrd_start >= kernel_end || initrd_end <= kernel_start))
    {
        ZF_LOGE("Linux kernel image [0x%lx..0x%lx) overlaps"
                " with the destination of the initial RAM disk [0x%lx, 0x%lx)\n",
                kernel_start, kernel_end, initrd_start, initrd_end);
        return 0;
    }
    // Check that the DTB and initrd do not overlap
    if (!(initrd_start >= dtb_end || initrd_end <= dtb_start))
    {
        ZF_LOGE("DTB [0x%lx..0x%lx) overlaps with the destination of the"
                " initial RAM disk [0x%lx, 0x%lx)\n",
                dtb_start, dtb_end, initrd_start, initrd_end);
        return 0;
    }
    // First we inspect the kernel image header to confirm it is a valid image
    // and to determine where in memory to place the image.
    struct linux_image_header *image_header = (struct linux_image_header *)kernel;
    assert(image_header->magic == LINUX_IMAGE_MAGIC);
    if (image_header->magic != LINUX_IMAGE_MAGIC)
    {
        ZF_LOGE("Linux kernel image magic check failed\n");
        return 0;
    }
    // Copy the guest kernel image into the right location
    uintptr_t kernel_dest = ram_start + image_header->text_offset;
    // This check is because the Linux kernel image requires to be placed at text_offset of
    // a 2MB aligned base address anywhere in usable system RAM and called there.
    // In this case, we place the image at the text_offset of the start of the guest's RAM,
    // so we need to make sure that the start of guest RAM is 2MiB aligned.
    assert((ram_start & ((1 << 20) - 1)) == 0);
    ZF_LOGI("Copying guest kernel image to 0x%lx (0x%lx bytes)\n", kernel_dest, kernel_size);
    memcpy((char *)kernel_dest, (char *)kernel, kernel_size);
    // Copy the guest device tree blob into the right location
    // First check that the DTB given is actually a DTB!
    struct dtb_header *dtb_header = (struct dtb_header *)dtb_src;
    assert(dtb_check_magic(dtb_header));
    if (!dtb_check_magic(dtb_header))
    {
        ZF_LOGE("Given DTB does not match DTB magic.\n");
        return 0;
    }
    // Linux does not allow the DTB to be greater than 2 megabytes in size.
    assert(dtb_size <= (1 << 21));
    if (dtb_size > (1 << 21))
    {
        ZF_LOGE("Linux expects size of DTB to be less than 2MB, DTB size is 0x%lx bytes\n", dtb_size);
        return 0;
    }
    // Linux expects the address of the DTB to be on an 8-byte boundary.
    assert(dtb_dest % 0x8 == 0);
    if (dtb_dest % 0x8)
    {
        ZF_LOGE("Linux expects DTB address to be on an 8-byte boundary, DTB address is 0x%lx\n", dtb_dest);
        return 0;
    }
    // ZF_LOGI("Copying guest DTB to 0x%lx (0x%lx bytes)\n", dtb_dest, dtb_size);
    // memcpy((char *)dtb_dest, (char *)dtb_src, dtb_size);
    // Copy the initial RAM disk into the right location
    // @ivanv: add checks for initrd according to Linux docs
    ZF_LOGI("Copying guest initial RAM disk to 0x%lx (0x%lx bytes)\n", initrd_dest, initrd_size);
    memcpy((char *)initrd_dest, (char *)initrd_src, initrd_size);

    return kernel_dest;
}
