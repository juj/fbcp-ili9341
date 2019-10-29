obj-m:= bcm2835_spi_display.o

CFLAGS_bcm2835_spi_display.o := -O3 -std=gnu99 -Wno-declaration-after-statement -DKERNEL_MODULE=1 -DILI9341=1 -DADAFRUIT_ILI9341_PITFT=1 -DSPI_BUS_CLOCK_DIVISOR=48 

KDIR ?= /lib/modules/$(shell uname -r)

all $(obj-m:%.o=%.ko):
	$(MAKE) -C "$(KDIR)/build" M="$(CURDIR)" modules

install: stop $(obj-m:%.o=%.ko)
	$(MAKE) -C "$(KDIR)/build" M="$(CURDIR)" modules_install
	@depmod -a

uninstall: stop
	@rm -vf -- "$(KDIR)/extra/$(obj-m:%.o=%.ko)" 
	@depmod -a

start: 
	@modprobe -v $(obj-m:%.o=%)

stop:
	@pgrep "fbcp-ili9341.*" && pkill "fbcp-ili9341.*" || true
	@pgrep "fbcp-ili9341.*" && sleep 5 && pkill -KILL "fbcp-ili9341.*" || true
	@modprobe -vr $(obj-m:%.o=%) || true

status:
	@modinfo $(obj-m:%.o=%)
	@echo loaded:
	@lsmod | grep $(obj-m:%.o=%) || echo not loaded

debug: $(obj-m:%.o=%.ko)
	@objdump -dS $< > $(obj-m:%.o=%.S)

clean:
	$(MAKE) -C "$(KDIR)/build" M="$(CURDIR)" clean
	@rm -rf -- $(obj-m:%.o=%.S)
