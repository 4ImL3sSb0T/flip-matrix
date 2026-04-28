#ifndef _IMU_SERVICE_H_
#define _IMU_SERVICE_H_

#include  "service/tools/vec_math.h"
#include "service/tools/common_def.h"

typedef struct
{
    exit_code_t (*imu_init)();
    exit_code_t (*imu_deinit)();
    exit_code_t (*imu_get_acc)(vec3f* acc);
    exit_code_t (*imu_get_gyro)(vec3f* gyro);
    exit_code_t (*imu_get_mag)(vec3f* mag);
    bool is_initialized;
} imu_sensor_t;

typedef enum
{
    IMU_SERVICE_WITH_MAG = 0,
    IMU_SERVICE_WITHOUT_MAG = 1
} imu_mode_t;

exit_code_t imu_service_init(imu_sensor_t* imu_sensor);
exit_code_t imu_service_start();
exit_code_t imu_service_deinit();

exit_code_t imu_service_get_euler(vec3f* euler);
exit_code_t imu_service_get_raw(vec3f* acc, vec3f* gyro, vec3f* mag);

imu_mode_t imu_service_get_mode();
exit_code_t imu_service_set_mode(imu_mode_t mode);

#endif

