#ifndef CONTROLLER_H
#define CONTROLLER_H
#include "lithium.h"
void controller_inputs(li_inputs_t *inputs);
void controller_tick(void);
void controller_apply_outputs(void);
uint8_t controller_idle(void);
void controller_stop(void);
#endif
