// OS-A09 practice P4: one entry per process for getprocs().
struct procinfo {
  int pid;
  int state; // enum procstate: 1 USED 2 SLEEPING 3 RUNNABLE 4 RUNNING 5 ZOMBIE
  uint64 sz; // user memory bytes
  char name[16];
};
