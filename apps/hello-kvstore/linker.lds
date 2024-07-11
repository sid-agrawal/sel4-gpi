/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */


SECTIONS
{
  .lib_data_prefix (NOLOAD) :
  {
    . = .;
  }
  .lib_data : ALIGN(4096)
  {
    apps/sel4test-driver/sel4test-tests/sqlite3/libsqlite3.a(.data)
    apps/sel4test-driver/kvstore/libkvstore_server_lib.a(.data)
  }
}
INSERT AFTER .data;