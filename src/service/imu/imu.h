#ifdef __IMU_H_
#define __IMU_H_

#include "../tools/common_def.h"
#include "../tools/vec_math.h"

exit_code_t init_imu(vec3f (*imu_get_acc)(), vec3f (*imu_get_gyro)(), vec3f (*imu_get_mag)());
exit_code_t imu_task();

#endif

