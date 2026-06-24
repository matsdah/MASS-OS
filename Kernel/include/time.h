#ifndef _TIME_H_
#define _TIME_H_

void sleep(unsigned long ms);
unsigned long deltaTicks(void);
void timer_handler(void);
void date(unsigned char *buff);
void time(unsigned char *buff);

#endif
