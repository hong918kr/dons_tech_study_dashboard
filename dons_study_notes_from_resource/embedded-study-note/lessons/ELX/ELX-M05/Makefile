# Kbuild: make -C /var/lab/out/linux-build-dbg M=$PWD MOD=solution modules
# (MOD = starter | solution | practice | demo_bugs). Vermagic 6.18.55-dbg: boots only with Image-dbg.
MOD ?= solution
obj-m := $(MOD).o
ccflags-y := -Werror
