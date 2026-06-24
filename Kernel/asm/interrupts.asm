GLOBAL _cli
GLOBAL _sti
GLOBAL picMasterMask
GLOBAL picSlaveMask
GLOBAL haltcpu
GLOBAL _hlt
GLOBAL load_idt_asm
GLOBAL _irq00Handler
GLOBAL _irq01Handler
GLOBAL _irq02Handler
GLOBAL _irq03Handler
GLOBAL _irq04Handler
GLOBAL _irq05Handler
GLOBAL _irq128Handler
GLOBAL _exception0Handler
GLOBAL _exception6Handler
GLOBAL syscallIntRoutine
GLOBAL pressed_key
GLOBAL regsArray
GLOBAL kbd_scancode_read
GLOBAL scheduler_start_asm

EXTERN irqDispatcher
EXTERN int80Dispatcher
EXTERN exceptionDispatcher
EXTERN getStackBase
EXTERN syscalls
EXTERN scheduler_tick
EXTERN scheduler_yield_impl
EXTERN force_switch

SECTION .text

%macro pushState 0
	push rax
	push rbx
	push rcx
	push rdx
	push rbp
	push rdi
	push rsi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15
%endmacro

%macro popState 0
	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rsi
	pop rdi
	pop rbp
	pop rdx
	pop rcx
	pop rbx
	pop rax
%endmacro

%macro irqHandlerMaster 1
	pushState

	mov rdi, %1 ; pasaje de parametro
	call irqDispatcher

	mov al, 20h
	out 20h, al

	popState
	iretq
%endmacro

%macro exceptionHandler 1
	pushState

	; Guardar snapshot de registros en una excepcion
	; Layout tras pushState: [r15..rax] luego frame de hardware (RIP,CS,RFLAGS)
	mov     rax, [rsp + 14*8]   ; RAX original
	mov     [regsArray + 0*8], rax
	mov     rax, [rsp + 13*8]   ; RBX
	mov     [regsArray + 1*8], rax
	mov     rax, [rsp + 12*8]   ; RCX
	mov     [regsArray + 2*8], rax
	mov     rax, [rsp + 11*8]   ; RDX
	mov     [regsArray + 3*8], rax
	mov     rax, [rsp + 10*8]   ; RBP
	mov     [regsArray + 4*8], rax
	mov     rax, [rsp + 9*8]    ; RDI
	mov     [regsArray + 5*8], rax
	mov     rax, [rsp + 8*8]    ; RSI
	mov     [regsArray + 6*8], rax
	mov     rax, [rsp + 7*8]    ; R8
	mov     [regsArray + 7*8], rax
	mov     rax, [rsp + 6*8]    ; R9
	mov     [regsArray + 8*8], rax
	mov     rax, [rsp + 5*8]    ; R10
	mov     [regsArray + 9*8], rax
	mov     rax, [rsp + 4*8]    ; R11
	mov     [regsArray + 10*8], rax
	mov     rax, [rsp + 3*8]    ; R12
	mov     [regsArray + 11*8], rax
	mov     rax, [rsp + 2*8]    ; R13
	mov     [regsArray + 12*8], rax
	mov     rax, [rsp + 1*8]    ; R14
	mov     [regsArray + 13*8], rax
	mov     rax, [rsp + 0*8]    ; R15
	mov     [regsArray + 14*8], rax

	; Frame de hardware (ring 0: solo RIP, CS, RFLAGS)
	mov     rax, [rsp + 15*8]   ; RIP
	mov     [regsArray + 15*8], rax
	mov     rax, [rsp + 16*8]   ; CS
	mov     [regsArray + 16*8], rax
	mov     rax, [rsp + 17*8]   ; RFLAGS
	mov     [regsArray + 17*8], rax
	; Los slots 18 y 19 (RSP/SS) no aplican en ring 0, quedan en 0
	mov     qword [regsArray + 18*8], 0
	mov     qword [regsArray + 19*8], 0

	mov rdi, %1
	mov rsi, rsp
	call exceptionDispatcher

	popState
	call getStackBase

	mov qword [rsp+8*3], rax
	mov qword [rsp], userland
	iretq
%endmacro

_hlt:
	sti
	hlt
	ret

_cli:
	cli
	ret

_sti:
	sti
	ret

; C-callable wrapper to load IDT without using inline asm in C
load_idt_asm:
	lidt    [rdi]
	ret

picMasterMask:
	push    rbp
   	mov     rbp, rsp
    	mov     ax, di
    	out	   21h,al
    	pop     rbp
    	retn

picSlaveMask:
	push    rbp
    	mov     rbp, rsp
    	mov     ax, di
    	out	   0A1h, al
    	pop     rbp
    	retn

; ─── Timer IRQ0: context switch preemptivo ────────────────────────────────────
; scheduler_tick(current_rsp) guarda el RSP actual, elige el siguiente proceso
; y devuelve su RSP en RAX. Hacemos el swap de RSP aqui en ASM.
_irq00Handler:
	pushState

	mov rdi, rsp
	call scheduler_tick     ; retorna RSP del proximo proceso
	mov rsp, rax            ; cambiar al stack del proximo proceso

	mov al, 20h
	out 20h, al             ; EOI al PIC master

	popState
	iretq

;Keyboard
; IRQ1 - Teclado: captura scancode y opcionalmente snapshot de registros.
_irq01Handler:
	pushState

	xor     eax, eax
	in      al, 0x60
	mov     [pressed_key], rax

	cmp     al, SNAPSHOT_KEY
	jne     .no_snapshot

	; Layout tras pushState: r15..rax (15 regs) | RIP, CS, RFLAGS (frame ring 0)
	mov     rax, [rsp + 14*8]   ; RAX original
	mov     [regsArray + 0*8], rax
	mov     rax, [rsp + 13*8]   ; RBX
	mov     [regsArray + 1*8], rax
	mov     rax, [rsp + 12*8]   ; RCX
	mov     [regsArray + 2*8], rax
	mov     rax, [rsp + 11*8]   ; RDX
	mov     [regsArray + 3*8], rax
	mov     rax, [rsp + 10*8]   ; RBP
	mov     [regsArray + 4*8], rax
	mov     rax, [rsp + 9*8]    ; RDI
	mov     [regsArray + 5*8], rax
	mov     rax, [rsp + 8*8]    ; RSI
	mov     [regsArray + 6*8], rax
	mov     rax, [rsp + 7*8]    ; R8
	mov     [regsArray + 7*8], rax
	mov     rax, [rsp + 6*8]    ; R9
	mov     [regsArray + 8*8], rax
	mov     rax, [rsp + 5*8]    ; R10
	mov     [regsArray + 9*8], rax
	mov     rax, [rsp + 4*8]    ; R11
	mov     [regsArray + 10*8], rax
	mov     rax, [rsp + 3*8]    ; R12
	mov     [regsArray + 11*8], rax
	mov     rax, [rsp + 2*8]    ; R13
	mov     [regsArray + 12*8], rax
	mov     rax, [rsp + 1*8]    ; R14
	mov     [regsArray + 13*8], rax
	mov     rax, [rsp + 0*8]    ; R15
	mov     [regsArray + 14*8], rax

	mov     rax, [rsp + 15*8]   ; RIP
	mov     [regsArray + 15*8], rax
	mov     rax, [rsp + 16*8]   ; CS
	mov     [regsArray + 16*8], rax
	mov     rax, [rsp + 17*8]   ; RFLAGS
	mov     [regsArray + 17*8], rax
	mov     qword [regsArray + 18*8], 0
	mov     qword [regsArray + 19*8], 0

.no_snapshot:
	mov     rdi, 1
	call    irqDispatcher
	mov     al, 20h
	out     20h, al

	cmp     qword [force_switch], 0
	je      .no_yield

	mov     qword [force_switch], 0
	mov     rdi, rsp
	call    scheduler_yield_impl
	mov     rsp, rax

.no_yield:
	popState
	iretq

;Cascade pic never called
_irq02Handler:
	irqHandlerMaster 2

;Serial Port 2 and 4
_irq03Handler:
	irqHandlerMaster 3

;Serial Port 1 and 3
_irq04Handler:
	irqHandlerMaster 4

;USB
_irq05Handler:
	irqHandlerMaster 5

;Zero Division Exception
_exception0Handler:
	exceptionHandler 0

;Invalid Opcode Exception
_exception6Handler:
	exceptionHandler 6

; ─── Syscall gate (int 0x80) ──────────────────────────────────────────────────
; El retorno de la syscall se escribe en el slot RAX del stack (rsp+14*8)
; para que popState lo restaure correctamente incluso tras un context switch.
; Si force_switch == 1, se hace un yield voluntario antes de retornar a userland.
_irq128Handler:
	pushState

	cmp rax, 40                 ; CANT_SYS = 40 (índices 0..39 válidos)
	jge .invalid_syscall

	call [syscalls + rax * 8]
	mov [rsp + 14*8], rax       ; guardar retorno en el slot RAX del stack
	jmp .check_yield

.invalid_syscall:
	mov qword [rsp + 14*8], -1  ; syscall invalida → retornar -1

.check_yield:
	cmp qword [force_switch], 0
	je  .no_yield

	mov qword [force_switch], 0
	mov rdi, rsp
	call scheduler_yield_impl   ; retorna RSP del proximo proceso
	mov rsp, rax

.no_yield:
	popState
	iretq

; ─── scheduler_start_asm ──────────────────────────────────────────────────────
; void scheduler_start_asm(uint64_t *rsp)
; Carga el RSP del primer proceso (pasado en rdi) y lo despacha.
; No retorna nunca.
scheduler_start_asm:
	mov rsp, rdi
	popState
	iretq

haltcpu:
	cli
	hlt
	ret

; Lee el scancode almacenado por la ISR y lo limpia
kbd_scancode_read:
	push rbp
	mov rbp, rsp
	xor eax, eax
	mov al, [pressed_key]
	mov byte [pressed_key], 0
	pop rbp
	ret

SECTION .bss
	aux resq 1
	regsArray resq 20
	pressed_key resq 1
	SNAPSHOT_KEY equ 0x1D

SECTION .data
	userland equ 0x400000
