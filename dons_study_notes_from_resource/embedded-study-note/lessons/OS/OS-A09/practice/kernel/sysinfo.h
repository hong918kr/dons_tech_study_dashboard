// OS-A09: shared by kernel (sysproc.c) and user programs.
struct sysinfo {
  uint64 freemem; // bytes of free physical memory
  uint64 nproc;   // processes whose state is not UNUSED
  uint64 nopenfiles; // practice P2: open-file table entries in use
};
