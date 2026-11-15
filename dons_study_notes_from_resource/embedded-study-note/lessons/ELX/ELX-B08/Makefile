# ELX-B08: module build. VARIANT = starter | solution | practice | demo_nocheck
# 모듈 이름은 kecho 로 고정한다 (kecho.o <- $(VARIANT).c 를 복사).
VARIANT ?= solution
KDIR ?= /var/lab/out/linux-build

obj-m := kecho.o
ccflags-y := -Werror

all:
	cp $(VARIANT).c kecho.c
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f kecho.c
