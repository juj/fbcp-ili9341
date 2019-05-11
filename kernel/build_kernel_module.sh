sudo ./stop_kernel_module.sh
sudo make VERBOSE=1 -C /lib/modules/$(uname -r)/build M=$(pwd) -I/usr/include/ modules

#For debugging: generate disassembly output:
#objdump -dS bcm2835_spi_display.ko > bcm2835_spi_display.S

