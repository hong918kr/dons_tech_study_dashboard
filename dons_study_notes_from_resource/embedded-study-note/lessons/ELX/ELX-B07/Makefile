# ELX-B07: module build. VARIANT = starter | solution | practice | demo
# The module is always called kecho.ko (built from $(VARIANT).c), except
# VARIANT=demo which builds demo_bugs.ko (the broken variants for Part B/C).
VARIANT ?= solution
KDIR ?= /var/lab/out/linux-build-dbg

ifeq ($(VARIANT),demo)
obj-m := demo_bugs.o
else
obj-m := kecho.o
kecho-y := $(VARIANT).o
endif
ccflags-y := -Werror

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) VARIANT=$(VARIANT) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) VARIANT=$(VARIANT) clean
