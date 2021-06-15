
Simple dma exmaple.

1. Apply the kernel patch to reserve enough memory. Without reserved memory, dma allocate might fail if the memory is too large.
2. build the dmacpy driver. 
3. build the test app


Usage
1. insmod dmacpy_drv.ko
2. show src and dst buffers 
	cat /sys/module/dmacpy_drv/parameters/show
3. execute test app which random overide src and dst buffer
4. trigger dma copy
	echo 1 > /sys/module/dmacpy_drv/parameters/dmacpy



file list
├── dmacpy_user_test.c
├── dmacpy_drv.c
├── kernel_5.10.9_reserved_mem.patch
├── Makefile
└── readme.txt

