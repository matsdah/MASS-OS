#include <stdint.h>
#include "keyboardDriver.h"
#include "irqDispatcher.h"

// IRQ0 (timer) ya no pasa por aqui: scheduler_tick en interrupts.asm lo maneja.
// IRQ1 (teclado) sigue aqui.
void irqDispatcher(uint64_t irq){
    switch(irq){
        case 1:
            handlePressedKey();
            break;
        default:
            break;
    }
}
