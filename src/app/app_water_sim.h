#ifndef APP_WATER_SIM_H
#define APP_WATER_SIM_H

#include "service/tools/common_def.h"
#include "service/matrix/matrix.h"
#include "service/flip/flip_core.h"
#include "FreeRTOS.h"

exit_code_t app_water_sim_init(void);
exit_code_t app_water_sim_start(void);
exit_code_t app_water_sim_stop(void);

void app_water_sim_set_gravity(float scale);
void app_water_sim_set_solver(int push_iters, int pressure_iters, float flip_ratio);
void app_water_sim_set_color_scheme(int scheme);
void app_water_sim_set_dt(float dt);
void app_water_sim_set_brightness(float percent);
void app_water_sim_status(void);

#endif