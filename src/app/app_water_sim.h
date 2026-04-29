#ifndef APP_WATER_SIM_H
#define APP_WATER_SIM_H

#include "service/tools/common_def.h"
#include "service/matrix/matrix.h"
#include "service/flip/flip_core.h"
#include "FreeRTOS.h"

exit_code_t app_water_sim_init(void);
exit_code_t app_water_sim_start(void);
exit_code_t app_water_sim_stop(void);

#endif