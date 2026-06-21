#include <stdint.h>
#include "../include/shell.h"
#include "../include/userlib.h"
#include "../include/test_util.h"

int64_t help(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    shellPrintString("Comandos disponibles: \n");
    shellPrintString("help                      ->   muestra la lista de comandos.\n");
    shellPrintString("clear                     ->   limpia la pantalla. (built-in) \n");
    shellPrintString("+                         ->   aumenta tamano de fuente (atajo de teclado).\n");
    shellPrintString("-                         ->   disminuye tamano de fuente (atajo de teclado).\n");
    shellPrintString("printTime                 ->   imprime la hora actual. (built-in) \n");
    shellPrintString("printDate                 ->   imprime la fecha actual. (built-in) \n");
    shellPrintString("registers                 ->   imprime registros. (built-in) \n");
    shellPrintString("testDiv0                  ->   division por cero. (built-in) \n");
    shellPrintString("invOp                     ->   instruccion invalida. (built-in) \n");
    shellPrintString("playBeep                  ->   reproduce un beep. (built-in) \n");
    shellPrintString("test_mm <max> [&]         ->   test de memory manager. (foreground/background)\n");
    shellPrintString("test_processes <max> [&]  ->   test de procesos. (foreground/background)\n");
    shellPrintString("test_prio <target> [&]    ->   test de prioridades. (foreground/background)\n");
    shellPrintString("test_sync <n> <sem> [&]   ->   test de sincronizacion. (foreground/background)\n");
    shellPrintString("mem                       ->   muestra estado de la memoria.\n");
    shellPrintString("kill <pid>                ->   termina el proceso indicado.\n");
    shellPrintString("nice <pid> <prio>         ->   cambia prioridad (1-5).\n");
    shellPrintString("block <pid>               ->   alterna estado BLOCKED/READY.\n");
    shellPrintString("loop                      ->   imprime PID periodicamente.\n");
    shellPrintString("sh                        ->   nueva shell.\n");
    shellPrintString("cat                       ->   copia stdin a stdout.\n");
    shellPrintString("wc                        ->   cuenta lineas, palabras y bytes de stdin. CTRL + D para finalizar.\n");
    shellPrintString("mvar <esc> <lec>          ->   MVar: escritores/lectores sincronizados.\n");
    shellPrintString("filter                    ->   filtra vocales del stdin.\n");
    shellPrintString("ps                        ->   lista de procesos activos.\n");

    return 0;
}
