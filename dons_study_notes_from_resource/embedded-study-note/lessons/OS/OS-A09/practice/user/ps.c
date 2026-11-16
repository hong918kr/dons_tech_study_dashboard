/* ENV: qemu (OS-A09 overlay on xv6-riscv fa2ddcb; built + booted by check.sh inside the Lima VM) */
// ps: list processes via getprocs() (practice P4).
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/procinfo.h"
#include "user/user.h"

static char *states[] = {"unused", "used", "sleep", "runble", "run", "zombie"};

int
main(void)
{
  struct procinfo pi[NPROC];
  int n = getprocs(pi, NPROC);
  if (n < 0) {
    fprintf(2, "ps: getprocs failed\n");
    exit(1);
  }
  printf("PID STATE NAME SZ\n");
  for (int i = 0; i < n; i++) {
    char *st = (pi[i].state >= 0 && pi[i].state < 6) ? states[pi[i].state] : "?";
    printf("%d %s %s %ld\n", pi[i].pid, st, pi[i].name, pi[i].sz);
  }
  exit(0);
}
