TARGET ?= qemu
BOOTLOADER_PATH=../Bootloader
BMFS=$(BOOTLOADER_PATH)/BMFS/bmfs.bin
MBR=$(BOOTLOADER_PATH)/Pure64/bmfs_mbr.sys
MP=../Toolchain/ModulePacker/mp.bin
PURE64=$(BOOTLOADER_PATH)/Pure64/pure64.sys
OSIMAGENAME=x64BareBonesImage
VMDK=$(OSIMAGENAME).vmdk
QCOW2=$(OSIMAGENAME).qcow2
IMG=$(OSIMAGENAME).img
KERNEL=../Kernel/kernel.bin
USERLAND=../Userland/0000-sampleCodeModule.bin ../Userland/0001-sampleDataModule.bin

PACKEDKERNEL=packedKernel.bin
IMGSIZE=6291456

ifeq ($(TARGET),qemu)
all: $(QCOW2)
else ifeq ($(TARGET),vbox)
all: $(VMDK)
else ifeq ($(TARGET),usb)
all: $(IMG)
else
$(error Unknown TARGET '$(TARGET)'; use qemu|vbox|usb)
endif

$(KERNEL):
	$(MAKE) -C ../Kernel

$(MP):
	$(MAKE) -C ../Toolchain all

$(PACKEDKERNEL): $(MP) $(KERNEL) $(USERLAND)
	$(MP) $(KERNEL) $(USERLAND) -o $(PACKEDKERNEL)

$(IMG): $(BMFS) $(MBR) $(PURE64) $(PACKEDKERNEL)
	$(BMFS) $(IMG) initialize $(IMGSIZE) $(MBR) $(PURE64) $(PACKEDKERNEL) 

$(VMDK): $(IMG)
	qemu-img convert -f raw -O vmdk $(IMG) $(VMDK)

$(QCOW2): $(IMG)
	qemu-img convert -f raw -O qcow2 $(IMG) $(QCOW2)

clean:
	rm -rf $(IMG) $(VMDK) $(QCOW2) *.bin

.PHONY: all clean