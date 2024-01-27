cmake_minimum_required(VERSION 3.7.2)
enable_language(C ASM)
set(CMAKE_VERBOSE_MAKEFILE ON)
project(hello C CXX)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

find_program(DTC_TOOL dtc)
    if("${DTC_TOOL}" STREQUAL "DTC_TOOL-NOTFOUND")
        message(FATAL_ERROR "Cannot find 'dtc' program.")
    endif()

set(
    VmDTSPath "${CMAKE_CURRENT_SOURCE_DIR}/board/qemu_arm_virt/linux.dts" # TODO: don't hardcode this
    CACHE INTERNAL "Location of VM DTS file"
    )

set(
    VmDTBPath "${CMAKE_CURRENT_BINARY_DIR}/linux.dtb"
)

# Compile DTS to DTB
execute_process(
    COMMAND
        echo ${DTC_TOOL} -I dts -O dtb ${VmDTSPath} > ${VmDTBPath}
    RESULT_VARIABLE error
)
if(error)
    message(FATAL_ERROR "Failed to compile DTS to DTB: ${VmDTSPath}")
endif()

file(
    GLOB
        deps
        src/*.c
)

string(CONCAT package_guest_image_defs
    "GUEST_KERNEL_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/board/qemu_arm_virt/linux\"\;"
    "GUEST_DTB_IMAGE_PATH=\"${VmDTBPath}\"\;"
    "GUEST_INITRD_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/board/qemu_arm_virt/rootfs.cpio.gz\"\;")

set_source_files_properties(
    tools/package_guest_images.S
    PROPERTIES
    COMPILE_OPTIONS "-x;assembler-with-cpp;"
    COMPILE_DEFINITIONS ${package_guest_image_defs}
)

list(
    APPEND
    deps
    ${VmDTBPath}
    tools/package_guest_images.S
)

add_executable(vmm EXCLUDE_FROM_ALL ${deps})

target_include_directories(
    vmm
    PRIVATE include src src/arch/aarch64 src/util src/microkit
)

target_link_libraries(
    vmm
    PUBLIC muslc sel4 sel4_autoconf sel4utils
    PRIVATE sel4test-driver_Config
)
