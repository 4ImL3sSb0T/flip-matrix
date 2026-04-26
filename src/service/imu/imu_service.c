#include "imu_service.h"
#include "MadgwickAHRS/MadgwickAHRS.h"

static imu_sensor_t* imu_sensor_handler = NULL;
static imu_mode_t imu_mode = IMU_SERVICE_WITHOUT_MAG;
static vec3f euler_angle = {0.0f, 0.0f, 0.0f};

exit_code_t imu_service_init(imu_sensor_t* imu_sensor) {
    if (imu_sensor == NULL || imu_sensor->imu_init == NULL || imu_sensor->imu_get_acc == NULL ||
        imu_sensor->imu_get_gyro == NULL || imu_sensor->imu_get_mag == NULL) return EXIT_INVALID_PARAM;
    imu_sensor_handler = imu_sensor;
    imu_sensor_handler->imu_init();
    return EXIT_OK;
}
exit_code_t imu_service_update(float dt) {
    // Madgwick
    return EXIT_OK;
}
exit_code_t imu_service_get_euler(vec3f* euler) {
    if (imu_sensor_handler == NULL) return EXIT_NOT_INITIALIZED;
    return EXIT_OK;
}
exit_code_t imu_service_get_sensor(vec3f* acc, vec3f* gyro, vec3f* mag) {
    if (imu_sensor_handler == NULL) return EXIT_NOT_INITIALIZED;
    imu_sensor_handler->imu_get_acc(acc);
    imu_sensor_handler->imu_get_gyro(gyro);
    imu_sensor_handler->imu_get_mag(mag);
    return EXIT_OK;
}
exit_code_t imu_service_deinit() {
    if (imu_sensor_handler == NULL || imu_sensor_handler->imu_deinit == NULL) return EXIT_NOT_INITIALIZED;
    imu_sensor_handler->imu_deinit();
    return EXIT_OK;
}

imu_mode_t imu_service_get_mode() {
    return imu_mode;
}
exit_code_t imu_service_set_mode(const imu_mode_t mode) {
    if (mode > IMU_SERVICE_WITHOUT_MAG) return EXIT_INVALID_PARAM;
    imu_mode = mode;
    return EXIT_OK;
}