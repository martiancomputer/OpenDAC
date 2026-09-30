#ifndef OPENDAC_DFU_H
#define OPENDAC_DFU_H
#include <stdbool.h>
void dfu_request(void);
bool dfu_pending(void);
void dfu_enter_rom(void) __attribute__((noreturn));
#endif
