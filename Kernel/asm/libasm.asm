GLOBAL getSeconds
GLOBAL getMinutes
GLOBAL getHour
GLOBAL getDayOfMonth
GLOBAL getMonth
GLOBAL getYear
GLOBAL inb
GLOBAL outb
GLOBAL atomic_xchg

section .text
	
getSeconds:
	mov al, 0
	out 0x70, al
	in al, 0x71
	ret

getMinutes:
	mov al, 2
	out 0x70, al
	in al, 0x71
	ret

getHour:
	mov al, 4
	out 0x70, al
	in al, 0x71
	ret

getMonth:
	mov al, 8
	out 0x70, al
	in al, 0x71
	ret

getYear:
	mov al, 9
	out 0x70, al
	in al, 0x71
	ret

getDayOfMonth:
	mov al, 7
	out 0x70, al
	in al, 0x71
	ret

outb:
	push rbp
	mov rbp, rsp

	mov dx, di
	mov al, sil
	out dx, al

	pop rbp
	ret

inb:
	push rbp	
	mov rbp, rsp

	mov dx, di 
	in al, dx 

	pop rbp
	ret

; uint64_t atomic_xchg(volatile uint64_t *ptr, uint64_t newval)
; Intercambia atómicamente *ptr <-> newval; retorna el valor viejo.
; xchg con operando de memoria tiene LOCK implícito en x86.
atomic_xchg:
	mov rax, rsi          ; rax = newval
	xchg rax, [rdi]       ; atomic: rax <- old [rdi], [rdi] <- newval
	ret