#include <xv6fs/defs.h>

#define PIPESIZE 512

struct pipe {
};

void
pipeclose(struct pipe *pi, int writable)
{
  return;
}

int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  return -1;
}

int
piperead(struct pipe *pi, uint64 addr, int n)
{
  return -1;
}
