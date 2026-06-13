obj-m += satellitefs.o
satellitefs-objs := satellitefs_main.o satellites.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
