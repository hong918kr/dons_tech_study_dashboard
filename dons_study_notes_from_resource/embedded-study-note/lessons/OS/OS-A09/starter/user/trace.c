/* ENV: qemu (OS-A09 overlay on xv6-riscv fa2ddcb; built + booted by check.sh inside the Lima VM) */
// trace MASK CMD [ARGS...]: set the trace mask, then exec CMD.
// The mask survives exec (same struct proc) and is inherited by fork.
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc < 3 || argv[1][0] < '0' || argv[1][0] > '9') {
    fprintf(2, "usage: trace MASK CMD [ARGS...]\n");
    exit(1);
  }
  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "trace: trace() failed\n");
    exit(1);
  }
  exec(argv[2], &argv[2]);
  fprintf(2, "trace: exec %s failed\n", argv[2]);
  exit(1);
}
