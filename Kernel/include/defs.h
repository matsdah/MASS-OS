#ifndef _defs_
#define _defs_

/* Flags para derechos de acceso de los segmentos */
#define ACS_PRESENT     0x80       /* segmento presente en memoria */
#define ACS_CSEG        0x18       /* segmento de codigo */
#define ACS_DSEG        0x10       /* segmento de datos */
#define ACS_READ        0x02       /* segmento de lectura */
#define ACS_WRITE       0x02       /* segmento de escritura */
#define ACS_IDT         ACS_DSEG
#define ACS_INT_386 	0x0E		/* Interrupt GATE 32 bits */
#define ACS_INT         ( ACS_PRESENT | ACS_INT_386 )
#define ACS_DPL_3       0x60
#define ACS_CODE        (ACS_PRESENT | ACS_CSEG | ACS_READ)
#define ACS_DATA        (ACS_PRESENT | ACS_DSEG | ACS_WRITE)
#define ACS_STACK       (ACS_PRESENT | ACS_DSEG | ACS_WRITE)

#define MIN_CHAR 0
#define MAX_CHAR 256
#define CANT_SYS 44
#define TTY_RAW   0
#define TTY_COOKED 1
#define STDOUT 1
#define STDERR 2
#define TEXT_SIZE 1
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define X_UPDATE FONT_WIDTH * TEXT_SIZE
#define Y_UPDATE FONT_HEIGHT * TEXT_SIZE
#define BUFF_LENGTH 256
#define REGISTERS_BUFFER_SIZE 1024
#define KBD_LENGTH 128
#define L_SHIFT 0x2A
#define R_SHIFT 0x36
#define CAPS_LOCK 0x3A
#define BREAK_CODE 0x80
#define L_ARROW 0x4B
#define R_ARROW 0x4D
#define UP_ARROW 0x48
#define DOWN_ARROW 0x50
#define BREAKCODE_OFFSET 128
#define L_CONTROL 0x1D
#define LETTERS 26
#define BACKSPACE 0x0E
#define MAX_FONT_SIZE 5

#endif
