# ELX-M04: module build. VARIANT = starter | solution | practice
VARIANT ?= solution
KDIR ?= /var/lab/out/linux-build-dbg

obj-m := $(VARIANT).o
ccflags-y := -Werror

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) VARIANT=$(VARIANT) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) VARIANT=$(VARIANT) clean
