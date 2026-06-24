include Makefile.inc

MODULE=0000-sampleCodeModule.bin
SAMPLE_DATA=0001-sampleDataModule.bin

SOURCES=$(wildcard _loader.c c/*.c c/tests/*.c c/shell/*.c c/commands/*.c)
SOURCES_ASM=$(wildcard asm/*.asm)
OBJECTS=$(SOURCES:.c=.o)
OBJECTS_ASM=$(SOURCES_ASM:.asm=.o)

all: $(MODULE) sampleDataModule

$(MODULE): $(OBJECTS) $(OBJECTS_ASM)
	$(LD) $(LDFLAGS) -T sampleCodeModule.ld -o $(MODULE) $(OBJECTS) $(OBJECTS_ASM)

%.o: %.c
	$(GCC) $(GCCFLAGS) -Ic/include -c $< -o $@

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -rf asm/*.o c/*.o c/tests/*.o c/shell/*.o c/commands/*.o *.o $(MODULE) $(SAMPLE_DATA)

sampleDataModule:
	printf "This is sample data." >> $(SAMPLE_DATA) && dd if=/dev/zero bs=1 count=1 >> $(SAMPLE_DATA)

.PHONY: all clean sampleDataModule