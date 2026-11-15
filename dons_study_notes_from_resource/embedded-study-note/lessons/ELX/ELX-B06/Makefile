# ELX-B06: out-of-tree Kbuild for two (practice: three) modules from one file.
#   make VARIANT=solution            -> hello.ko hello_user.ko (Linux 6.18.55)
#   make VARIANT=solution KDIR=/var/lab/out/linux-build-dbg  -> vermagic 6.18.55-dbg
# Kbuild reads this file a second time (from the kernel tree, KERNELRELEASE set);
# the "ifneq" splits the two passes.
VARIANT ?= solution
KDIR ?= /var/lab/out/linux-build

ifneq ($(KERNELRELEASE),)
# ---- pass 2: Kbuild ------------------------------------------------------
obj-m := hello.o hello_user.o
ifeq ($(VARIANT),practice)
obj-m += hello_opt.o
endif
CFLAGS_hello.o      := -DHELLO_PART_CORE
CFLAGS_hello_user.o := -DHELLO_PART_USER
CFLAGS_hello_opt.o  := -DHELLO_PART_OPT
ccflags-y := -Werror

else
# ---- pass 1: the make you type ------------------------------------------
PARTS := hello hello_user $(if $(filter practice,$(VARIANT)),hello_opt)

all:
	for p in $(PARTS); do cp $(VARIANT).c $$p.c; done
	$(MAKE) -C $(KDIR) M=$(CURDIR) VARIANT=$(VARIANT) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f hello.c hello_user.c hello_opt.c
endif
