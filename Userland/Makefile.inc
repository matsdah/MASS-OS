GCC=x86_64-linux-gnu-gcc
GPP=x86_64-linux-gnu-g++
LD=x86_64-linux-gnu-ld
AR=x86_64-linux-gnu-ar
ASM=nasm

# Harden warnings to help catch dead code and API leaks
GCCFLAGS=-m64 -fno-exceptions -std=c99 -Wall -Wextra -Werror -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wformat -Wstrict-prototypes -Wno-unused-parameter -ffreestanding -nostdlib -fno-common -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -fno-builtin-malloc -fno-builtin-free -fno-builtin-realloc -fno-pie
ARFLAGS=rvs
ASMFLAGS=-felf64
