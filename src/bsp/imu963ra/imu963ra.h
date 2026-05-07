#ifndef _IMU963RA_H_
#define _IMU963RA_H_

#include "service/tools/vec_math.h"
#include "service/tools/common_def.h"

typedef struct {
    vec3f acc;
    vec3f gyro;
    vec3f mag;
} imu963ra_data_t;

exit_code_t imu963ra_init(void);
exit_code_t imu963ra_deinit(void);
exit_code_t imu963ra_read_acc(vec3f* acc);
exit_code_t imu963ra_read_gyro(vec3f* gyro);
exit_code_t imu963ra_read_mag(vec3f* mag);

#endif
