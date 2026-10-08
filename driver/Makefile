# SPDX-License-Identifier: GPL-2.0-only
#
# Out-of-tree build for the Honeycomb Bravo HID driver.
#   make                      build against the running kernel
#   make KVER=<version>       build against another installed kernel
#   make clean

obj-m := hid-honeycomb-bravo.o

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
PWD  := $(shell pwd)

# BTF for modules needs pahole (Debian package "dwarves"). If it is not
# installed, skip BTF generation instead of failing; the module works fine
# without it.
ifeq ($(shell command -v pahole 2>/dev/null),)
EXTRA_KBUILD_ARGS += CONFIG_DEBUG_INFO_BTF_MODULES=
endif

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) $(EXTRA_KBUILD_ARGS) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all modules clean
