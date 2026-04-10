KDIR= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m += edu.o
edu-objs := main.o edu_pci.o edu_char.o edu_mmio.o

all:
	make -C $(KDIR) M=$(PWD) modules
install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
clean:
	make -C $(KDIR) M=$(PWD) clean