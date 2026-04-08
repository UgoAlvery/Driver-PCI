obj-m += edu.o

edu-objs := main.o edu_pci.o edu_mmio.o edu_char.o

KDIR := /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean