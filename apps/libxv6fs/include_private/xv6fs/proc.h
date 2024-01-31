#pragma once

// Per-process state
struct proc {
  int pid;                     // Process ID
  struct inode *cwd;           // Current directory
  char cwd_path[64];
};