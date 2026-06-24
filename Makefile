TARGET ?= qemu
MM     ?= FF

# Exportamos para que TODOS los sub-makes (Kernel/Image/etc.) hereden la
# configuración. Sin esto, Kernel/Makefile siempre compilaría con First-Fit
# aunque se pase MM=BUDDY al make raíz.
export TARGET
export MM

all: bootloader kernel userland toolchain image

bootloader:
	$(MAKE) -C Bootloader all

kernel:
	$(MAKE) -C Kernel all

userland:
	$(MAKE) -C Userland all

toolchain:
	$(MAKE) -C Toolchain all

image: kernel bootloader userland toolchain
	$(MAKE) -C Image all

# Alias de compatibilidad: `make buddy` = `make MM=BUDDY all`.
buddy:
	$(MAKE) MM=BUDDY all

clean:
	$(MAKE) -C Bootloader clean
	$(MAKE) -C Image      clean
	$(MAKE) -C Kernel     clean
	$(MAKE) -C Toolchain  clean
	$(MAKE) -C Userland   clean

.PHONY: all bootloader kernel userland toolchain image buddy clean
