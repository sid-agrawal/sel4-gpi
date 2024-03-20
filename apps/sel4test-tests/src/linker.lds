/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

SECTIONS
{
  .sqlite3_data :
  {
    apps/sel4test-driver/sel4test-tests/sqlite3/libsqlite3.a(.data)
  }
  .kvsever_data :
  {
    apps/sel4test-driver/kvstore/libkvstore_client.a(.data)
  }
}
INSERT AFTER .data;