include Makefile.inc

KERNEL=kernel.bin
SOURCES_ASM=$(wildcard asm/*.asm)
OBJECTS_ASM=$(SOURCES_ASM:.asm=.o)
LOADERSRC=loader.asm
LOADEROBJECT=$(LOADERSRC:.asm=.o)
STATICLIBS=

# Variable para elegir memory manager (por defecto: first fit)
MM ?= FF

ifeq ($(MM), BUDDY)
    MM_SRC = c/memoryManager/memoryManagerBuddy.c
    MM_FLAG = -DMM_BUDDY
else
    MM_SRC = c/memoryManager/memoryManagerFF.c
    MM_FLAG = -DMM_FF
endif

SOURCES=$(wildcard c/*.c c/drivers/*.c c/scheduler/*.c c/process/*.c c/semaphore/*.c c/pipe/*.c c/interrupts/*.c c/syscall/*.c c/lib/*.c c/sound/*.c c/kernel/*.c) $(MM_SRC)
OBJECTS=$(SOURCES:.c=.o)

all: $(KERNEL)

$(KERNEL): $(LOADEROBJECT) $(OBJECTS) $(STATICLIBS) $(OBJECTS_ASM)
	$(LD) $(LDFLAGS) -T kernel.ld -o $(KERNEL) $(LOADEROBJECT) $(OBJECTS) $(OBJECTS_ASM) $(STATICLIBS)

%.o: %.c
	$(GCC) $(GCCFLAGS) -I./include -c $< -o $@

%.o : %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

$(LOADEROBJECT):
	$(ASM) $(ASMFLAGS) $(LOADERSRC) -o $(LOADEROBJECT)

clean:
	rm -rf asm/*.o c/*.o c/drivers/*.o c/memoryManager/*.o c/scheduler/*.o c/process/*.o c/semaphore/*.o c/pipe/*.o c/interrupts/*.o c/syscall/*.o c/lib/*.o c/sound/*.o c/kernel/*.o *.o *.bin

.PHONY: all clean
