# ELX-M06: make -C /var/lab/out/linux-build-dbg M=$PWD MOD=solution modules
obj-m := $(if $(MOD),$(MOD).o,starter.o solution.o practice.o)
ccflags-y := -Werror
