#include <stdint.h>
#include "lib.h"
#include "moduleLoader.h"
#include "idtLoader.h"
#include "kernelApi.h"
#include "memoryManager.h"
#include "process.h"
#include "scheduler.h"
#include "interrupts.h"
#include "semaphore.h"
#include "fd.h"
#include "pipe.h"
#include "mvar.h"

#define HEAP_START  0x600000
#define HEAP_SIZE   (8 * 1024 * 1024)  // 8 MB

extern uint8_t text;
extern uint8_t rodata;
extern uint8_t data;
extern uint8_t bss;
extern uint8_t endOfKernelBinary;
extern uint8_t endOfKernel;
static const uint64_t PageSize = 0x1000;
static void * const sampleCodeModuleAddress = (void*)0x400000;
static void * const sampleDataModuleAddress = (void*)0x500000;

// Proceso idle: ejecuta hlt en loop. Siempre READY, nunca se bloquea.
static void idle_entry(int argc, char **argv) {
    (void)argc; (void)argv;
    while (1)
        _hlt();
}

// Punto de entrada del kernel: arranca el scheduler (nunca retorna)
int main(void){
    /* Si scheduler_start retorna quedar en loop. */ 
    scheduler_start();  
    while(1){
        _hlt();
    }
    
    return 0;
}

void clearBSS(void * bssAddress, uint64_t bssSize){
    memset(bssAddress, 0, bssSize);
}

void * getStackBase(void){
    return (void*)(
        (uint64_t)&endOfKernel
        + PageSize * 8
        - sizeof(uint64_t)
    );
}

// Carga modulos, inicializa subsistemas y crea los procesos iniciales
void * initializeKernelBinary(void){
    void * moduleAddresses[] = {sampleCodeModuleAddress, sampleDataModuleAddress};

    loadModules(&endOfKernelBinary, moduleAddresses);
    clearBSS(&bss, &endOfKernel - &bss);
    load_idt();

    mm_init((void *)HEAP_START, HEAP_SIZE);

    process_init();
    fd_init();
    pipe_init();
    scheduler_init();
    sem_init();
    mvar_init();

    // Crear proceso idle primero (siempre en la posicion 0 de la cola)
    process_create("idle", idle_entry, 0, NULL, 0);

    // Crear la shell como proceso foreground
    process_create("shell", (ProcessEntry)sampleCodeModuleAddress, 0, NULL, 1);

    return getStackBase();
}
