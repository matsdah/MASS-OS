#include <stdint.h>
#include "idtLoader.h"
#include "defs.h"
#include "interrupts.h"

#pragma pack(push)
#pragma pack (1)

typedef struct{
  uint16_t offset_l, selector;
  uint8_t cero, access;
  uint16_t offset_m;
  uint32_t offset_h, other_cero;
} DESCR_INT;

typedef struct {
  uint16_t limit;
  uint64_t base;
} IDTR;

#pragma pack(pop)

static DESCR_INT idt[256] = {0};
static void setup_IDT_entry (int index, uint64_t offset);

// Inicializa la IDT con handlers básicos y habilita interrupciones
void load_idt(void){
  setup_IDT_entry(0x20, (uint64_t)&_irq00Handler);      // Timer
  setup_IDT_entry(0x21, (uint64_t)&_irq01Handler);      // Keyboard
  setup_IDT_entry(0x00, (uint64_t)&_exception0Handler); // #DE
  setup_IDT_entry(0x06, (uint64_t)&_exception6Handler); // #UD
  setup_IDT_entry(0x80, (uint64_t)&_irq128Handler);     // Syscalls

  // Load IDTR
  IDTR idtr;
  idtr.limit = sizeof(idt) - 1;
  idtr.base = (uint64_t)idt;
  load_idt_asm(&idtr);

  // Mask PIC
  picMasterMask(0xFC);
  picSlaveMask(0xFF);

  _sti();
}

// Configura una entrada de la IDT en modo 64 bits (interrupt gate)
static void setup_IDT_entry (int index, uint64_t offset){
  idt[index].selector = 0x08;
  idt[index].offset_l = offset & 0xFFFF;
  idt[index].offset_m = (offset >> 16) & 0xFFFF;
  idt[index].offset_h = (offset >> 32) & 0xFFFFFFFF;
  idt[index].access = ACS_INT;
  idt[index].cero = 0;
  idt[index].other_cero = (uint64_t) 0;
}