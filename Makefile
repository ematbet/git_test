obj-m := myhello.o

KERNELDIR := "/usr/src/kernels/"$(shell uname -r)
PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean