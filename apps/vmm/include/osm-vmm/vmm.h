#pragma once
#include <vmm-common/vmm_common.h>

/**
 * @brief starts a new linux guest as a PD.
 * Does not support any other type of guest because we currently don't need to
 *
 * @return int 0 on success, 1 on failure
 */
int new_guest(void);
