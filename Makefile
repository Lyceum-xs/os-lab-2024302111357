CC = riscv64-unknown-elf-gcc
LD = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
NM = riscv64-unknown-elf-nm
PYTHON = python3
QEMU = qemu-system-riscv64
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector \
         -fno-pie -msmall-data-limit=0 -MMD -MP
CFLAGS += $(EXTRA_CFLAGS)
UFLAGS = $(CFLAGS) -I. -Iuser
OBJS = kernel/entry.o kernel/start.o kernel/main.o kernel/string.o \
       kernel/console.o kernel/printf.o kernel/uart.o kernel/plic.o \
       kernel/trap.o kernel/proc.o kernel/syscall.o kernel/sysproc.o \
       kernel/exec.o kernel/trampoline.o kernel/userimg.o kernel/swtch.o \
       kernel/kernelvec.o kernel/kalloc.o kernel/vm.o kernel/vmtest.o
UPROGS = sh hi spin badecall bufstorm badaccess memcap vmguard vmcopy vmstress vmconcurrent
ULIB = user-flat/ulib.o user-flat/printf.o user-flat/usys.o

.DEFAULT_GOAL := kernel/kernel
.PHONY: clean qemu test test-kernel test-uart dump
.SECONDARY:

kernel/kernel: $(OBJS) kernel/kernel-sv39.ld
	$(LD) -T kernel/kernel-sv39.ld -o $@ $(OBJS)

kernel/kernel-sv39.ld: kernel/kernel.ld support/build_images.py
	$(PYTHON) support/build_images.py linker kernel $< $@

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

kernel/%.o: kernel/%.S
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -nographic

user/usys.S: user/usys.pl kernel/syscall.h
	perl user/usys.pl > $@

user-flat:
	mkdir -p $@

user-flat/user-sv39.ld: user/user.ld support/build_images.py | user-flat
	$(PYTHON) support/build_images.py linker user $< $@

vpath %.c user tests support/tests
user-flat/%.o: %.c | user-flat
	$(CC) $(UFLAGS) -c -o $@ $<

user-flat/usys.o: user/usys.S | user-flat
	$(CC) $(UFLAGS) -c -o $@ $<

user-flat/%.elf: user-flat/%.o $(ULIB) user-flat/user-sv39.ld
	$(LD) -T user-flat/user-sv39.ld -o $@ $< $(ULIB)

user-flat/%.bin: user-flat/%.elf
	$(OBJCOPY) -O binary $< $@

kernel/userimg.S: Makefile support/build_images.py $(addprefix user-flat/,$(UPROGS:=.bin))
	$(PYTHON) support/build_images.py images --nm $(NM) --output $@ $(UPROGS)

test: kernel/kernel
	$(PYTHON) support/test_lab3.py

fs.img:
	dd if=/dev/zero of=$@ bs=1024 count=1024

test-uart: kernel/kernel fs.img
	$(PYTHON) support/inject_uart.py --tree . --script support/storm.txt --log inject_uart.log

test-kernel:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS=-DLAB3_SELFTEST kernel/kernel
	$(PYTHON) support/test_lab3.py --kernel-only
	$(MAKE) clean
	$(MAKE) kernel/kernel

dump: kernel/kernel
	$(PYTHON) support/test_lab3.py --dump-only

clean:
	rm -f kernel/*.o kernel/*.d kernel/kernel kernel/kernel-sv39.ld kernel/userimg.S user/usys.S
	rm -rf user-flat

-include $(wildcard kernel/*.d user-flat/*.d)
