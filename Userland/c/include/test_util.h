#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdint.h>

uint32_t GetUint(void);
uint32_t GetUniform(uint32_t max);
uint8_t  memcheck(void *start, uint8_t value, uint32_t size);
int64_t  satoi(char *str);
int      is_valid_uint(const char *str);
void     bussy_wait(uint64_t n);
void     endless_loop(int argc, char *argv[]);
void     endless_loop_print(int argc, char *argv[]);

/* Wrappers de shell para los tests */
void     test_mm_cmd(void);
void     test_processes_cmd(void);
void     test_prio_cmd(void);
void     test_sync_cmd(void);

/* Entry points de procesos para el registry */
void     zero_to_max(int argc, char *argv[]);
int64_t  my_process_inc(int argc, char *argv[]);

/* Entry points para spawneados como procesos hijos */
int64_t  test_mm_entry(int argc, char *argv[]);
int64_t  test_processes_entry(int argc, char *argv[]);
int64_t  test_prio_entry(int argc, char *argv[]);
int64_t  test_sync_entry(int argc, char *argv[]);

int64_t  mem(int argc, char *argv[]);
int64_t  kill_cmd(int argc, char *argv[]);
int64_t  nice_cmd(int argc, char *argv[]);
int64_t  block_cmd(int argc, char *argv[]);
int64_t  sh_entry(int argc, char *argv[]);
int64_t  cat(int argc, char *argv[]);
int64_t  wc(int argc, char *argv[]);
void     mvar_writer(int argc, char *argv[]);
void     mvar_reader(int argc, char *argv[]);
int64_t  mvar(int argc, char *argv[]);
int64_t  user_mvar_create(const char *name);
int64_t  user_mvar_put(const char *name, char value);
int64_t  user_mvar_take(const char *name);
void     user_mvar_destroy(const char *name);
int64_t  filter(int argc, char *argv[]);
int64_t  help(int argc, char *argv[]);

/* Provisto por _loader.c */
void    *memset(void *s, int32_t c, uint64_t n);

int      printf(const char *fmt, ...);

#endif
