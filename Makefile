obj-m += edu.o
edu-objs := main.o edu_pci.o edu_char.o edu_mmio.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean