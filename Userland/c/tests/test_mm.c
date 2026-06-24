#include "../include/syscall.h"
#include "../include/test_util.h"
#include "../include/userlib.h"
#include "../include/shell.h"

/* Número máximo de bloques de memoria que se pueden allocar en una sola prueba. */
#define MAX_BLOCKS 128

/* Estructura para representar una solicitud de memoria. */
typedef struct MM_rq{
    void *address;
    uint32_t size;
}mm_rq;

/* Función interna para ejecutar la prueba de gestión de memoria. */
static int64_t test_mm_internal(uint64_t argc, char *argv[]){
    mm_rq mm_rqs[MAX_BLOCKS];
    uint8_t rq;
    uint32_t total;
    uint64_t max_memory;
    uint32_t i;

    if(argc != 1){
        shellPrintString("uso: test_mm <max_memory>\n");
        return -1;
    }

    if(!is_valid_uint(argv[0])){
        shellPrintString("test_mm: max_memory debe ser un numero positivo\n");
        return -1;
    }

    if((max_memory = (uint64_t)satoi(argv[0])) == 0){
        shellPrintString("test_mm: max_memory debe ser mayor a 0\n");
        return -1;
    }

    while(1){
        rq = 0;
        total = 0;

        while((rq < MAX_BLOCKS) && (total < max_memory)){
            /* Solicita un bloque de memoria aleatorio dentro de lo permitido. */
            mm_rqs[rq].size = GetUniform((uint32_t)(max_memory - total - 1)) + 1;
            mm_rqs[rq].address = malloc(mm_rqs[rq].size);

            if(mm_rqs[rq].address){
                total += mm_rqs[rq].size;
                rq++;
            }else{
                break;
            }
        }

        /* Rellena los bloques de memoria solicitados. */
        for(i = 0; i < rq; i++){
            if(mm_rqs[i].address){
                memset(mm_rqs[i].address, (int32_t)i, mm_rqs[i].size);
            }
        }

        /* Verifica que los bloques de memoria estén correctamente rellenos. */
        for(i = 0; i < rq; i++){
            if(mm_rqs[i].address){
                if(!memcheck(mm_rqs[i].address, (uint8_t)i, mm_rqs[i].size)){
                    printf("test_mm ERROR.\n");
                    return -1;
                }
            }
        }

        /* Libera los bloques de memoria solicitados. */
        for(i = 0; i < rq; i++){
            if (mm_rqs[i].address){
                free(mm_rqs[i].address);
            }
        }
    }
}

/* Wrapper de shell: test_mm <max_memory> */
void test_mm_cmd(void){
    const char *args = cmd_args();

    if(!args){
        shellPrintString("uso: test_mm <max_memory>\n");
        return;
    }

    char *argv[1] = {(char *)args};
    test_mm_internal(1, argv);
}

/* Entry point para ser spawneado como proceso hijo, o sea no built-in. */
int64_t test_mm_entry(int argc, char *argv[]){
    return test_mm_internal((uint64_t)argc, argv);
}
