# ELX-B11: test fixture module (fake I2C adapter with .xfer) for the 6.18.55-dom guest.
KDIR ?= /var/lab/out/linux-build-dom

obj-m := b11_fakei2c.o
ccflags-y := -Werror

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
