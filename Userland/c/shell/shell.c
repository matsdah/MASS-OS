#include "shell.h"
#include "userlib.h"

static int cursorVisible = 0;

static void showCursor(void){
    if(!cursorVisible){
        char cursor = CURSOR;
        sys_write(STDOUT, &cursor, 1);
        cursorVisible = 1;
    }
}

static void hideCursor(void){
    if(cursorVisible){
        char backspace = '\b';
        sys_write(STDOUT, &backspace, 1);
        cursorVisible = 0;
    }
}

// Bucle principal de la shell de usuario
void shell_run(void){
    shellPrintString(WELCOME);
    shellNewline();
    shellPrintString("Escriba 'help' para listar comandos.\n");

    char buff[BUFF_LENGTH];
    while(1){
        shellPrintString("> ");
        showCursor();
        shellReadLine(buff, BUFF_LENGTH);
        hideCursor();
        shellNewline();
        processLine(buff, 0);
    }
}

int main(void){
    shell_run();
    return 0;
}

// Lee una línea desde teclado con cursor parpadeante
void shellReadLine(char * buffer, uint64_t max){
    char c;
    uint32_t idx = 0;
    uint64_t lastBlink = sys_ticks();
    const uint64_t blinkInterval = 9; // ~0.5s si 18 ticks ~ 1s

    // Asegurar que el cursor esté visible al iniciar la lectura
    showCursor();

    while(1){
        uint64_t n = sys_read(0, &c, 1);
        if(n == 1){
            if(c == '\n'){
                break;
            }

            if(c == '\b'){
                if(idx > 0){
                    idx--;
                    hideCursor();               // remover cursor '_'
                    shellPutchar('\b', STDOUT); // borrar último carácter
                    showCursor();               // volver a dibujar cursor
                }
            } else if(c == '+'){
                // Aumentar fuente y redibujar contenido
                hideCursor();
                sys_increase_fontsize();
                redrawFont();
                showCursor();
            } else if(c == '-'){
                // Disminuir fuente y redibujar contenido
                hideCursor();
                sys_decrease_fontsize();
                redrawFont();
                showCursor();
            } else {
                if(idx + 1 < max){ // dejar lugar para terminador NUL
                    buffer[idx++] = c;
                    hideCursor();
                    shellPutchar(c, STDOUT);
                    showCursor();
                }
            }

            // Reiniciar temporizador de parpadeo tras cualquier input
            lastBlink = sys_ticks();
        } else {
            // Sin input: gestionar parpadeo
            uint64_t now = sys_ticks();
            if(now - lastBlink >= blinkInterval){
                if(cursorVisible){
                    hideCursor();
                } else {
                    showCursor();
                }
                lastBlink = now;
            }
        }
    }

    buffer[idx] = 0;
}

// Imprime una cadena en STDOUT
void shellPrintString(char *str){
    if(str == 0){
        return;
    }
    for(uint32_t i = 0; str[i] != '\0'; i++){
        shellPutchar(str[i], STDOUT);
    }
}

// Escribe un caracter en el descriptor indicado
void shellPutchar(char c, uint64_t fd){
    // Registrar en redraw buffer antes de imprimir
    redraw_append_char(c, fd);
    sys_write(fd, &c, 1); // escribo el caracter
}

// Salto de línea
void shellNewline(void){
    shellPutchar('\n', STDOUT);
}