# Out-of-tree build against the lab kernel (in the Lima VM):
#   make            -> edu_lp06.ko + edu_mmap (static aarch64)
ifneq ($(KERNELRELEASE),)
obj-m := edu_lp06.o
else
KDIR ?= /var/lab/out/linux-build

all: modules edu_mmap

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

edu_mmap: edu_mmap.c
	gcc -static -O2 -Wall -Wextra -Werror $< -o $@

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f edu_mmap
endif
