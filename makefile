ASM?=nasm
CC=clang
LD?=ld.lld
LD_EFI?=lld-link
OBJCOPY?=llvm-objcopy

SRC_DIR=src
BUILD_DIR=build
UEFI_IMG?=testos.img
ESP_OFFSET?=1048576

EFI_TARGET?=x86_64-unknown-windows

CFLAGS_EFI=-std=c11 -target $(EFI_TARGET) -ffreestanding -fno-stack-protector -fno-pic \
           -mno-red-zone -I $(SRC_DIR)/uefi -I $(SRC_DIR)/kernel
LDFLAGS_EFI=/entry:efi_main /subsystem:efi_application /nodefaultlib /machine:x64

CFLAGS_KERNEL=-std=c11 -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
              -I $(SRC_DIR)/kernel
LDFLAGS_KERNEL=-nostdlib -T $(SRC_DIR)/kernel/linker.ld

.PHONY: all uefi_image kernel clean always

all: uefi_image

# UEFI image

uefi_image: $(BUILD_DIR)/$(UEFI_IMG)

$(BUILD_DIR)/$(UEFI_IMG): $(BUILD_DIR)/BOOTX64.EFI $(BUILD_DIR)/KERNEL.BIN
	dd if=/dev/zero of=$(BUILD_DIR)/$(UEFI_IMG) bs=1M count=64
	printf "g\nn\n1\n2048\n\n\nt\n1\nw\n" | fdisk $(BUILD_DIR)/$(UEFI_IMG)
	python3 tools/write_pmbr.py $(BUILD_DIR)/$(UEFI_IMG)
	mkfs.fat -F 32 --offset=2048 -h 2048 -n "NBOS" $(BUILD_DIR)/$(UEFI_IMG)
	mmd -i $(BUILD_DIR)/$(UEFI_IMG)@@$(ESP_OFFSET) ::/EFI ::/EFI/BOOT
	mcopy -i $(BUILD_DIR)/$(UEFI_IMG)@@$(ESP_OFFSET) $(BUILD_DIR)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(BUILD_DIR)/$(UEFI_IMG)@@$(ESP_OFFSET) $(BUILD_DIR)/KERNEL.BIN ::/EFI/BOOT/KERNEL.BIN

$(BUILD_DIR)/BOOTX64.EFI: $(BUILD_DIR)/boot.o $(BUILD_DIR)/jump.o $(BUILD_DIR)/kernel_blob.o
	$(LD_EFI) /out:$@ $(LDFLAGS_EFI) $^

$(BUILD_DIR)/boot.o: $(SRC_DIR)/uefi/boot.c | always
	$(CC) $(CFLAGS_EFI) -c $< -o $@

$(BUILD_DIR)/jump.o: $(SRC_DIR)/uefi/jump.S | always
	$(CC) $(CFLAGS_EFI) -c $< -o $@

# Kernel

kernel: $(BUILD_DIR)/KERNEL.BIN

$(BUILD_DIR)/KERNEL.BIN: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/kernel.o $(BUILD_DIR)/console.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/shell.o $(BUILD_DIR)/pci.o $(BUILD_DIR)/xhci.o $(BUILD_DIR)/block.o $(BUILD_DIR)/ahci.o $(BUILD_DIR)/nvme.o $(BUILD_DIR)/fat32.o $(BUILD_DIR)/kstart.o
	$(LD) $(LDFLAGS_KERNEL) $^ -o $@

$(BUILD_DIR)/kernel.o: $(SRC_DIR)/kernel/kernel.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/console.o: $(SRC_DIR)/kernel/console.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/keyboard.o: $(SRC_DIR)/kernel/keyboard.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/shell.o: $(SRC_DIR)/kernel/shell.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/pci.o: $(SRC_DIR)/kernel/drivers/pci.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/xhci.o: $(SRC_DIR)/kernel/drivers/xhci.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/kstart.o: $(SRC_DIR)/kernel/start.S | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/block.o: $(SRC_DIR)/kernel/drivers/block.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/ahci.o: $(SRC_DIR)/kernel/drivers/ahci.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/nvme.o: $(SRC_DIR)/kernel/drivers/nvme.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/fat32.o: $(SRC_DIR)/kernel/fs/fat32.c | always
	$(CC) $(CFLAGS_KERNEL) -c $< -o $@

#always

always:
	mkdir -p $(BUILD_DIR)

#clean

clean:
	rm -rf $(BUILD_DIR)/*
$(BUILD_DIR)/kernel_blob.c: $(BUILD_DIR)/KERNEL.BIN tools/bin2c.py | always
	python3 tools/bin2c.py $< $@

$(BUILD_DIR)/kernel_blob.o: $(BUILD_DIR)/kernel_blob.c | always
	$(CC) $(CFLAGS_EFI) -c $< -o $@
