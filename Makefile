KDIR= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m += edu.o
edu-objs := main.o edu_pci.o edu_char.o edu_mmio.o

# KUnit test module: compiled only when the target kernel has CONFIG_KUNIT=y.
# To run: boot a kernel built with CONFIG_KUNIT=y, then insmod edu_kunit.ko.
# Results appear in dmesg or via tools/testing/kunit/kunit.py.
obj-$(CONFIG_KUNIT) += edu_kunit.o

all:
	make -C $(KDIR) M=$(PWD) modules
install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
clean:
	make -C $(KDIR) M=$(PWD) clean
