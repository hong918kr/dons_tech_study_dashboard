# ELX-B09: module build. VARIANT = starter | solution | practice | demo_bugs
VARIANT ?= solution
KDIR ?= /var/lab/out/linux-build-dbg

obj-m := $(VARIANT).o
ccflags-y := -Werror $(EXTRA_CFLAGS_B09)

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) VARIANT=$(VARIANT) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) VARIANT=$(VARIANT) clean
