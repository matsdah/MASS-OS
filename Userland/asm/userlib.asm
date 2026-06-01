GLOBAL sys_write
GLOBAL sys_read
GLOBAL sys_registers
GLOBAL sys_time
GLOBAL sys_date
GLOBAL sys_clear
GLOBAL sys_increase_fontsize
GLOBAL sys_decrease_fontsize
GLOBAL sys_ticks
GLOBAL sys_beep
GLOBAL sys_speaker_start
GLOBAL sys_speaker_off
GLOBAL sys_screen_width
GLOBAL sys_screen_height
GLOBAL sys_putpixel
GLOBAL sys_fill_rect
GLOBAL gen_invalid_opcode
GLOBAL sys_malloc
GLOBAL sys_free
GLOBAL sys_mem_status
GLOBAL sys_create_process
GLOBAL sys_exit
GLOBAL sys_getpid
GLOBAL sys_ps
GLOBAL sys_kill
GLOBAL sys_nice
GLOBAL sys_block
GLOBAL sys_unblock
GLOBAL sys_yield
GLOBAL sys_waitpid
GLOBAL sys_sem_open
GLOBAL sys_sem_wait
GLOBAL sys_sem_post
GLOBAL sys_sem_close
GLOBAL sys_pipe
GLOBAL sys_dup2
GLOBAL sys_close
GLOBAL sys_pipe_open
GLOBAL sys_pipe_setup
GLOBAL sys_tty_mode
GLOBAL sys_write_color
GLOBAL sys_mvar_create
GLOBAL sys_mvar_put
GLOBAL sys_mvar_take
GLOBAL sys_mvar_destroy

section .text

sys_malloc:
    mov rax, 16
    int 0x80
    ret

sys_free:
    mov rax, 17
    int 0x80
    ret

sys_mem_status:
    mov rax, 18
    int 0x80
    ret

sys_create_process:
    mov rax, 19
    int 0x80
    ret

sys_exit:
    mov rax, 20
    int 0x80
    ret

sys_getpid:
    mov rax, 21
    int 0x80
    ret

sys_ps:
    mov rax, 22
    int 0x80
    ret

sys_kill:
    mov rax, 23
    int 0x80
    ret

sys_nice:
    mov rax, 24
    int 0x80
    ret

sys_block:
    mov rax, 25
    int 0x80
    ret

sys_unblock:
    mov rax, 26
    int 0x80
    ret

sys_yield:
    mov rax, 27
    int 0x80
    ret

sys_waitpid:
    mov rax, 28
    int 0x80
    ret

sys_sem_open:
    mov rax, 29
    int 0x80
    ret

sys_sem_wait:
    mov rax, 30
    int 0x80
    ret

sys_sem_post:
    mov rax, 31
    int 0x80
    ret

sys_sem_close:
    mov rax, 32
    int 0x80
    ret

sys_pipe:
    mov rax, 33
    int 0x80
    ret

sys_dup2:
    mov rax, 34
    int 0x80
    ret

sys_close:
    mov rax, 35
    int 0x80
    ret

sys_pipe_open:
    mov rax, 36
    int 0x80
    ret

sys_pipe_setup:
    mov rax, 37
    int 0x80
    ret

sys_tty_mode:
    mov rax, 38
    int 0x80
    ret

sys_registers:
	mov rax, 0
	int 0x80
	ret

sys_time:
	mov rax, 1
	int 0x80
	ret

sys_date:
	mov rax, 2
	int 0x80
	ret

sys_read:
	mov rax, 3
	int 0x80
	ret

sys_write:
	mov rax, 4
	int 0x80
	ret

sys_write_color:
	mov rax, 39
	int 0x80
	ret

sys_increase_fontsize:
    	mov rax, 5
	int 0x80
	ret

sys_decrease_fontsize:
   	mov rax, 6
	int 0x80
	ret

sys_beep:
    	mov rax, 7
	int 0x80
	ret

sys_ticks:
    mov rax, 8
    int 0x80
    ret

sys_clear:
    mov rax, 9
	int 0x80
	ret

sys_speaker_start:
	mov rax, 10
	int 0x80
	ret

sys_speaker_off:
	mov rax, 11
	int 0x80
	ret

sys_screen_width:
	mov rax, 12
	int 0x80
	ret

sys_screen_height:
	mov rax, 13
	int 0x80
	ret

sys_putpixel:
	mov rax, 14
	int 0x80
	ret

sys_fill_rect:
	mov rax, 15
	int 0x80
	ret

sys_mvar_create:
	mov rax, 40
	int 0x80
	ret

sys_mvar_put:
	mov rax, 41
	int 0x80
	ret

sys_mvar_take:
	mov rax, 42
	int 0x80
	ret

sys_mvar_destroy:
	mov rax, 43
	int 0x80
	ret

gen_invalid_opcode:
    ud2
    ret
