#include <stdint.h>
#include <stddef.h>
#include "../c/include/userlib.h"
#include "../c/include/shell.h"
#include "../c/include/syscall.h"
#include "include/test_util.h"

static Command commands[] = {
    {"help", help},
    {"clear", clear},
    {"printTime", printTime},
    {"printDate", printDate},
    {"registers", registers},
    {"testDiv0", divideByZero},
    {"invOp", invOp},
    {"playBeep", playBeep},
    {"bmFPS", bmFPS},
    {"bmCPU", bmCPU},
    {"bmMEM", bmMEM},
    {"bmKEY", bmKEY},
    {"test_mm", test_mm_cmd},
    {"test_processes", test_processes_cmd},
    {"test_prio", test_prio_cmd},
    {"test_sync", test_sync_cmd},
    {"test_named_pipe", test_named_pipe_cmd},
    {"ps", ps},
    {0, 0},
};

/* Argumentos del comando actual: lo setea processLine antes de invocar al
   comando, y se accede via cmd_args(). NULL si no hay argumentos. */
static const char *g_cmd_args = 0;

const char *cmd_args(void){
    return g_cmd_args;
}


RedrawStruct redrawBuffer[REDRAW_BUFF];
uint32_t redrawLength = 0;
void redraw_reset(void){
    redrawLength = 0;
}

void redraw_append_char(char c, uint64_t fd){
    if(redrawLength >= REDRAW_BUFF){
        // drop oldest
        for(uint32_t i = 1; i < redrawLength; i++){
            redrawBuffer[i-1] = redrawBuffer[i];
        }
        redrawLength--;
    }
    redrawBuffer[redrawLength].character = c;
    redrawBuffer[redrawLength].fd = fd;
    redrawLength++;
}


/* Convierte un entero sin signo a cadena en la base indicada.
   - value: número a convertir.
   - dest: buffer destino (debe tener espacio suficiente), se escribe NUL-terminated.
   - base: base entre 2 y 16 (por ejemplo 10 para decimal, 16 para hex).
   Retorna la cantidad de caracteres escritos (sin incluir el '\0').
*/
// Convierte entero sin signo a string en base [2..16]
uint64_t num_to_str(uint64_t value, char * dest, int base){
    if(!dest) return 0;
    if(base < 2 || base > 16) base = 10;

    char tmp[65];
    int pos = 0;

    if(value == 0){
        tmp[pos++] = '0';
    } else {
        while(value){
            int d = value % base;
            tmp[pos++] = (d < 10) ? ('0' + d) : ('A' + (d - 10));
            value /= base;
        }
    }

    /* volcar en orden correcto */
    for(int i = 0; i < pos; i++){
        dest[i] = tmp[pos - 1 - i];
    }
    dest[pos] = '\0';
    return (uint64_t)pos;
}

// Benchmark de CPU (operaciones int/float)
void bmCPU(){
    const uint32_t N = 1000000u;
    uint64_t ticks = sys_ticks();

    uint64_t result = 0;
    double float_result = 0.0;

    for(uint32_t i = 0; i < N; ++i){
        result += (uint64_t)i * 7ull;
        result = result % 2147483647ull;

        float_result += (double)i * 3.14159;
        if ((i % 100000u) == 0 && i != 0) {
            float_result *= 0.5;
        }
    }

    uint64_t end_ticks = sys_ticks();
    uint64_t delta = end_ticks - ticks;

    shellPrintString("Tiempo: ");

    char timeBuff[32];
    num_to_str(delta, timeBuff, 10);
    shellPrintString(timeBuff);
    shellPrintString(" ticks\n");

    if(delta > 0){
        /* promote N to 64-bit before division to avoid surprises */
        uint64_t ops_per_tick = ((uint64_t)N) / delta;
        shellPrintString("Operaciones por tick: ");
        num_to_str(ops_per_tick, timeBuff, 10);
        shellPrintString(timeBuff);
        shellPrintString("\n");
    } else {
        shellPrintString("Elapsed ticks = 0, no se puede calcular ops/tick.\n");
    }

    return;
}

// Benchmark de FPS aproximado (limpia pantalla en loop)
void bmFPS(){    
    uint64_t ticks = sys_ticks();
    uint64_t count = 0;
    /* 18 ticks ~= 1 second on many systems that emulate BIOS ticks */
    uint64_t duration = 18 * 7; /* ~3 seconds */

    shellPrintString("Inicio de test.\n");
    /* busy loop that only clears the screen and increments counter */
    while((sys_ticks() - ticks) < duration){
        sys_clear();
        count++;
    }

    /* count iterations over ~7 seconds -> approximate frames per second */
    uint64_t fps = count / 7;
    shellPrintString("FPS: ");

    char fpsBuff[BM_BUFF];
    num_to_str(fps, fpsBuff, 10);
    shellPrintString(fpsBuff);
    shellPrintString("\n");
}

// Benchmark simple de memoria (llenado/copia/checksum)
void bmMEM(){
    char buffer[4 * KB];
    uint64_t totalChecksum = 0;
    uint64_t ticks = sys_ticks();

    for(int iteration = 0; iteration < 10000; iteration++){
        for(int i = 0; i < 4 * KB; i++){
            buffer[i] = (i + iteration) % 256;
        }

        /* use 64-bit checksum to avoid truncation issues */
        uint64_t checksum = 0;
        for(int i = 0; i < 4 * KB; i++){
            checksum += (unsigned char)buffer[i];
            checksum = checksum % 1000000ULL;
        }

        for(int i = 0; i < 2 * KB; i++){
            buffer[i + 2 * KB] = buffer[i];
        }

        /* make checksum observable to prevent over-optimization */
        totalChecksum += checksum;
    }

    uint64_t finalTicks = sys_ticks();
    uint64_t delta = finalTicks - ticks;

    shellPrintString("Tiempo: ");

    char buff[BM_BUFF];
    num_to_str(delta, buff, 10);
    shellPrintString(buff);
    shellPrintString(" ticks\n");

    if(delta > 0){
        /* compute operations using 64-bit arithmetic to be safe */
        uint64_t operations = (uint64_t)10000 * (uint64_t)(4 * KB) * 3ULL;
        uint64_t operationsPerCycle = operations / delta;
        shellPrintString("Operaciones por tick: ");
        num_to_str(operationsPerCycle, buff, 10);
        shellPrintString(buff);
        shellPrintString("\n");
    }

    /* Print a checksum summary to ensure computations aren't optimized away */
    shellPrintString("Checksum: ");
    num_to_str(totalChecksum, buff, 10);
    shellPrintString(buff);
    shellPrintString("\n");
}

// Mide tiempo hasta presionar una tecla
void bmKEY(){
    shellPrintString("Presione cualquier tecla: \n");
    uint64_t ticks = sys_ticks();
    getchar();

    uint64_t finalTicks = sys_ticks();
    uint64_t delta = finalTicks - ticks;
    shellPrintString("Tiempo: ");
    char buff[BM_BUFF];

    num_to_str(delta, buff, 10);
    shellPrintString(buff);
    shellPrintString(" ticks\n");
}

// Reproduce una secuencia corta de beeps
void playBeep(){
    sys_beep(NOTE_E5, EIGHTH);
    sys_beep(NOTE_DS5, EIGHTH);
    sys_beep(NOTE_E5, EIGHTH);
    sys_beep(NOTE_DS5, EIGHTH);
    sys_beep(NOTE_E5, EIGHTH);
    sys_beep(NOTE_B4, EIGHTH);
    sys_beep(NOTE_D5, EIGHTH);
    sys_beep(NOTE_C5, EIGHTH);
    sys_beep(NOTE_A4, QUARTER);

    sys_beep(NOTE_C4, EIGHTH);
    sys_beep(NOTE_E4, EIGHTH);
    sys_beep(NOTE_A4, EIGHTH);
    sys_beep(NOTE_B4, QUARTER);

    sys_beep(NOTE_E4, EIGHTH);
    sys_beep(NOTE_GS4, EIGHTH);
    sys_beep(NOTE_B4, EIGHTH);
    sys_beep(NOTE_C5, QUARTER);

    sys_beep(NOTE_E4, EIGHTH);
    sys_beep(NOTE_E5, EIGHTH);
    sys_beep(NOTE_DS5, EIGHTH);
    sys_beep(NOTE_E5, EIGHTH);
    sys_beep(NOTE_DS5, EIGHTH);
    sys_beep(NOTE_E5, EIGHTH);
    sys_beep(NOTE_B4, EIGHTH);
    sys_beep(NOTE_D5, EIGHTH);
    sys_beep(NOTE_C5, EIGHTH);
    sys_beep(NOTE_A4, QUARTER);

    sys_beep(NOTE_C4, EIGHTH);
    sys_beep(NOTE_E4, EIGHTH);
    sys_beep(NOTE_A4, EIGHTH);
    sys_beep(NOTE_B4, QUARTER);

    sys_beep(NOTE_E4, EIGHTH);
    sys_beep(NOTE_C5, EIGHTH);
    sys_beep(NOTE_B4, EIGHTH);
    sys_beep(NOTE_A4, QUARTER);
}

// Redibuja la pantalla luego de cambiar el tamaño de fuente
void redrawFont(){
    sys_clear(); 

    if(redrawLength == 0){
        return;
    } 

    char buffer[REDRAW_BUFF]; 

    uint64_t current = redrawBuffer[0].fd;
    uint32_t idx = 0;

    for(uint32_t i = 0; i < redrawLength; i++){
        if(redrawBuffer[i].fd != current || idx >= sizeof(buffer) - 1){
            if(idx > 0){
                sys_write(current, buffer, idx);
                idx = 0;
            }
            current = redrawBuffer[i].fd;
        }
        buffer[idx++] = redrawBuffer[i].character;
    }
    
    if(idx > 0){
        sys_write(current, buffer, idx);
    }
}

// Aumenta tamaño de fuente y refresca contenido
void shellIncreaseFontSize(){
    sys_increase_fontsize(); 
    redrawFont();
}

// Disminuye tamaño de fuente y refresca contenido
void shellDecreaseFontSize(){ 
    sys_decrease_fontsize(); 
    redrawFont();
}

// Lista de comandos disponibles
void help(){
    shellPrintString("Comandos disponibles: \n");
    shellPrintString("help                      ->   muestra la lista de comandos.\n");
    shellPrintString("clear                     ->   limpia la pantalla.\n");
    shellPrintString("+                         ->   aumenta tamaño de fuente.\n");
    shellPrintString("-                         ->   disminuye tamaño de fuente.\n");
    shellPrintString("printTime                 ->   imprime la hora actual.\n");
    shellPrintString("printDate                 ->   imprime la fecha actual.\n");
    shellPrintString("registers                 ->   imprime registros.\n");
    shellPrintString("testDiv0                  ->   division por cero.\n");
    shellPrintString("invOp                     ->   instruccion invalida.\n");
    shellPrintString("playBeep                  ->   reproduce un beep.\n");
    shellPrintString("bmFPS                     ->   benchmark de FPS.\n");
    shellPrintString("bmCPU                     ->   benchmark de CPU.\n");
    shellPrintString("bmMEM                     ->   benchmark de MEM.\n");
    shellPrintString("bmKEY                     ->   benchmark de teclado.\n");
    shellPrintString("test_mm <max> [&]         ->   test de memory manager. (foreground/background)\n");
    shellPrintString("test_processes <max> [&]  ->   test de procesos. (foreground/background)\n");
    shellPrintString("test_prio <target> [&]    ->   test de prioridades. (foreground/background)\n");
    shellPrintString("test_sync <n> <sem> [&]   ->   test de sincronizacion. (foreground/background)\n");
    shellPrintString("test_named_pipe           ->   test de pipes con nombre. \n");
    shellPrintString("mem                       ->   muestra estado de la memoria.\n");
    shellPrintString("kill <pid>                ->   termina el proceso indicado.\n");
    shellPrintString("nice <pid> <prio>         ->   cambia prioridad (1-5).\n");
    shellPrintString("block <pid>               ->   alterna estado BLOCKED/READY.\n");
    shellPrintString("loop                      ->   imprime PID periodicamente.\n");
    shellPrintString("sh                        ->   nueva shell interactiva.\n");
    shellPrintString("cat                       ->   copia stdin a stdout.\n");
    shellPrintString("wc                        ->   cuenta lineas, palabras y bytes de stdin.\n");
    shellPrintString("mvar <esc> <lec>          ->   MVar: escritores/lectores sincronizados.\n");
    shellPrintString("filter                    ->   filtra vocales del stdin.\n");
    shellPrintString("ps                        ->   lista de procesos activos.\n");
}

// Limpia la pantalla
void clear(){
    sys_clear();
    redraw_reset();
}

// Provoca excepción de división por cero
void divideByZero(){
    clear();
    int x = 1;
    int y = 0;
    int z;
    z = x / y; // dispara #DE
    (void)z;   // evitar warning de variable no usada (si no se dispara la excepción)
}

void invOp(){
    gen_invalid_opcode();
}

// Imprime el snapshot de registros (CTRL para capturar)
void registers(){
    char buffer[REGSBUFF];

    if(sys_registers(buffer)){
        shellPrintString(buffer);
    } else{
        shellPrintString("Presione CTRL para guardar los registros.\n");
    }
}

// Ajusta hora BCD por offset (0-23)
uint8_t adjustHour(uint8_t hour, int offset){
    int decimalHour = ((hour >> 4) * 10) + (hour & 0x0F);
    decimalHour += offset;

     // Ajustar para que esté en el rango 0-23
    if (decimalHour < 0){
        decimalHour += 24;
    }else{
          if(decimalHour >= 24){
            decimalHour -= 24;
          }
    }

     return ((decimalHour / 10) << 4) | (decimalHour % 10);
}

// Imprime HH:MM:SS o DD/MM/AA desde buffer BCD
void printTimeAndDate(uint8_t* buff, char separator){
    char outBuff[10];

    for(int i = 0; i < 3; i++){
        int value = ((buff[i] >> 4) & 0x0F) * 10 + (buff[i] & 0x0F);
        outBuff[3 * i] = (char)('0' + (value / 10));
        outBuff[3 * i + 1] = (char)('0' + (value % 10));

        if(i < 2){
            outBuff[3 * i + 2] = separator;
        }
    }

    outBuff[8] = '\n';
    outBuff[9] = 0;

    shellPrintString(outBuff);
}

// Imprime hora local (UTC-3)
void printTime(){
    uint8_t timeBuff[3];
    sys_time(timeBuff);
    timeBuff[0] = adjustHour(timeBuff[0], -3);
    printTimeAndDate(timeBuff, ':');
}

// Imprime fecha local considerando rollover por UTC-3
void printDate(){
    uint8_t timeBuff[3];
    uint8_t dateBuff[3];

    sys_time(timeBuff);
    sys_date(dateBuff);

    int hour = ((timeBuff[0] >> 4) * 10) + (timeBuff[0] & 0x0F);

    if(hour < 3){
        int day = ((dateBuff[0] >> 4) * 10) + (dateBuff[0] & 0x0F);
        day--;
        
        if(day <= 0){
            day = 30;
            int month = ((dateBuff[1] >> 4) * 10) + (dateBuff[1] & 0x0F);
            month--;
            
            if(month <= 0){
               month = 12;
               int year = ((dateBuff[2] >> 4) * 10) + (dateBuff[2] & 0x0F);
               year--;
               dateBuff[2] = ((year / 10) << 4) | (year % 10);
            }

            dateBuff[1] = ((month / 10) << 4) | (month % 10);
        }

        dateBuff[0] = ((day / 10) << 4) | (day % 10);
    }

    printTimeAndDate(dateBuff, '/');
}

// Implementaciones mínimas de string para entorno freestanding
// strlen mínimo para entorno freestanding (interno a este módulo)
static size_t strlen(const char *s){
    size_t n = 0;
    if(s == 0) return 0;
    while(s[n] != '\0') n++;
    return n;
}

// strcmp mínimo para entorno freestanding (interno a este módulo)
static int strcmp(const char *a, const char *b){
    if(a == 0 && b == 0){
          return 0;
    }

    if(a == 0){
          return -1;
    }

    if(b == 0){
          return 1;
    }

    while(*a && (*a == *b)){
        a++; 
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

// putchar usando sys_write
uint64_t putchar(char c){
    char buff[1];
    buff[0] = c;
    redraw_append_char(c, STDOUT);
    return sys_write(STDOUT, buff, 1);
}

char getchar(){
    char c;
    uint64_t n;
    while((n = sys_read(0, &c, 1)) == (uint64_t)-1)
        ;
    return c;
}

/* Lista de comandos que se spawnean como procesos hijos en vez de
   ejecutarse in-process dentro de la shell. */
static int is_child_command(const char *name){
    static const char *child_cmds[] = {
        "test_mm", "test_processes", "test_prio", "test_sync",
        "np_writer", "np_reader", "mem", "kill", "nice", "block", "loop", "sh", "cat", "wc", "mvar", "filter", NULL
    };
    for(int i = 0; child_cmds[i]; i++)
        if(strcmp(name, child_cmds[i]) == 0)
            return 1;
    return 0;
}

/* Copia tokenizada de cmd_args en argv[]. Retorna argc. */
static int parse_args(char *src, char **argv, int max_argv){
    int argc = 0;

    while(src && *src && argc < max_argv){

        while(*src == ' '){
            src++;
        }

        if(*src == '\0'){
            /* Fin de la cadena */
            break;
        }

        argv[argc++] = src;
        
        while(*src && *src != ' '){
            src++;
        }

        if(*src == ' '){
            *src = '\0';
            src++;
        }
    }

    return argc;
}

/* Busca '|' en buff y lo parte en dos comandos.
   Retorna 1 si encontro pipe, 0 si no. */
static int parse_pipe(char *buff, char **left, char **right){
    size_t i;
    for(i = 0; buff[i]; i++){
        if(buff[i] == '|'){
            buff[i] = '\0';
            *left = buff;
            *right = &buff[i + 1];

            size_t l = strlen(*left);
            while(l > 0 && (*left)[l - 1] == ' ')
                (*left)[--l] = '\0';

            while(**right == ' ')
                (*right)++;

            return 1;
        }
    }
    return 0;
}

/* Ejecuta dos comandos conectados por pipe: stdout del izquierdo → stdin del derecho.
   Ambos comandos deben ser procesos hijos (child commands), no built-ins.
   Los procesos se crean BLOCKED (fg=3) para evitar race condition en la redireccion. */
static void handle_pipe(char *left, char *right, int bg){
    int fds[2];
    if(sys_pipe(fds) < 0){
        shellPrintString("Error creando pipe\n");
        return;
    }

    char *left_name = left;
    char *left_args = 0;
    for(size_t i = 0; left[i]; i++){
        if(left[i] == ' '){
            left[i] = '\0';
            size_t j = i + 1;
            while(left[j] == ' ') j++;
            if(left[j] != '\0') left_args = &left[j];
            break;
        }
    }

    char *right_name = right;
    char *right_args = 0;
    for(size_t i = 0; right[i]; i++){
        if(right[i] == ' '){
            right[i] = '\0';
            size_t j = i + 1;
            while(right[j] == ' ') j++;
            if(right[j] != '\0') right_args = &right[j];
            break;
        }
    }

    if(!is_child_command(left_name) || !is_child_command(right_name)){
        shellPrintString("Pipe solo soporta comandos externos\n");
        sys_close((uint64_t)fds[0]);
        sys_close((uint64_t)fds[1]);
        return;
    }

    char *argv1[16] = {0};
    int argc1 = 0;
    if(left_args)
        argc1 = parse_args(left_args, argv1, 15);

    char *argv2[16] = {0};
    int argc2 = 0;
    if(right_args)
        argc2 = parse_args(right_args, argv2, 15);

    int64_t pid1 = my_create_process_fg(left_name, (uint64_t)argc1, argv1, 3);
    if(pid1 < 0){
        shellPrintString("Error creando proceso izquierdo\n");
        sys_close((uint64_t)fds[0]);
        sys_close((uint64_t)fds[1]);
        return;
    }

    int64_t pid2 = my_create_process_fg(right_name, (uint64_t)argc2, argv2, 3);
    if(pid2 < 0){
        shellPrintString("Error creando proceso derecho\n");
        sys_kill((uint64_t)pid1);
        sys_close((uint64_t)fds[0]);
        sys_close((uint64_t)fds[1]);
        return;
    }

    sys_pipe_setup((uint64_t)pid1, (uint64_t)fds[1], 1);
    sys_pipe_setup((uint64_t)pid2, (uint64_t)fds[0], 0);

    sys_close((uint64_t)fds[0]);
    sys_close((uint64_t)fds[1]);

    if(!bg){
        sys_tty_mode(TTY_COOKED);
    }
    sys_unblock((uint64_t)pid1);
    sys_unblock((uint64_t)pid2);

    if(!bg){
        my_wait(pid1);
        my_wait(pid2);
        sys_tty_mode(TTY_RAW);
    } else {
        shellPrintString("[");
        char tmp[16];
        num_to_str((uint64_t)pid1, tmp, 10);
        shellPrintString(tmp);
        shellPrintString(", ");
        num_to_str((uint64_t)pid2, tmp, 10);
        shellPrintString(tmp);
        shellPrintString("]\n");
    }
}

/* Busca y ejecuta el comando ingresado. Los tests (test_mm, test_processes,
   test_prio, test_sync) se spawnean como procesos hijos. Si el comando
   termina con '&' se ejecuta en background; sino en foreground con waitpid.
   Soporta pipes entre comandos con '|'. */
void processLine(char *buff, uint32_t *history_len){
    (void)history_len;

    if(strlen(buff) == 0){
        return;
    }

    /* Detectar '&' al final (background) */
    int bg = 0;

    size_t len = strlen(buff);

    if(len > 0 && buff[len - 1] == '&'){

        bg = 1;
        buff[len - 1] = '\0';
        len--;

        while(((len > 0) && (buff[len - 1] == ' '))){
            buff[--len] = '\0';
        }
    }

    /* Detectar '|' para pipes entre comandos */
    char *pipe_left, *pipe_right;
    if(parse_pipe(buff, &pipe_left, &pipe_right)){
        handle_pipe(pipe_left, pipe_right, bg);
        return;
    }

    /* Separar nombre del comando y argumentos */
    g_cmd_args = 0;

    for(size_t i = 0; buff[i]; i++){
        
        if(buff[i] == ' '){
            buff[i] = '\0';
            size_t j = i + 1;

            while(buff[j] == ' '){
                j++;
            }
            
            if(buff[j] != '\0'){
                g_cmd_args = &buff[j];
            }

            break;
        }
    }

    /* ¿Es un comando que se spawnea como proceso hijo? */
    if(is_child_command(buff)){
        char *argv[16] = {0};
        int argc = 0;
        if(g_cmd_args)
            argc = parse_args((char *)g_cmd_args, argv, 15);

        if(!bg){
            shellPrintString("\b"); /* Ocultar cursor '_' antes de ceder foreground */
            sys_tty_mode(TTY_COOKED);
        }
        int64_t pid = my_create_process_fg(buff, (uint64_t)argc, argv, bg ? 0 : 1);
        if(pid < 0){
            shellPrintString("Error creando proceso\n");
        } else if(!bg){
            /* Foreground: esperar a que termine */
            my_wait(pid);
            sys_tty_mode(TTY_RAW);
        } else {
            /* Background: imprimir PID */
            shellPrintString("[");
            char tmp[16];
            num_to_str((uint64_t)pid, tmp, 10);
            shellPrintString(tmp);
            shellPrintString("]\n");
        }
    } else {
        /* Built-in: ejecutar in-process */
        for(int i = 0; commands[i].name != 0; i++){
            if(strcmp(buff, commands[i].name) == 0){
                commands[i].function();
                g_cmd_args = 0;
                return;
            }
        }
        shellPrintString("Comando no reconocido! Escriba 'help' para ver los comandos disponibles.\n");
    }
    g_cmd_args = 0;
}

static const char *state_names[] = {"FREE", "READY", "RUNNING", "BLOCKED", "ZOMBIE"};

/* Muestra la lista de procesos activos con PID, nombre, prioridad y estado. */ 
void ps(void){
    static ProcessInfo buf[MAX_PROCESSES];
    uint64_t count = sys_ps(buf, MAX_PROCESSES);
    char tmp[24];

    shellPrintString("PID  PRI  FG  STATE    NAME\n");
    shellPrintString("---  ---  --  -------  --------\n");

    for(uint64_t i = 0; i < count; i++){
        // PID
        num_to_str(buf[i].pid, tmp, 10);
        shellPrintString(tmp);
        shellPrintString("    ");

        // Prioridad
        num_to_str(buf[i].priority, tmp, 10);
        shellPrintString(tmp);
        shellPrintString("    ");

        // Foreground
        shellPrintString(buf[i].foreground ? "Y " : "N ");
        shellPrintString("  ");

        // Estado
        uint8_t st = buf[i].state;
        if(st <= 4){
            shellPrintString((char *)state_names[st]);
        } else{
            shellPrintString("?");
        }
        shellPrintString("  ");

        // Nombre
        shellPrintString(buf[i].name);
        shellPrintString("\n");
    }
}