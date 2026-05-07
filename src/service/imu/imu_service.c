#include "imu_service.h"
#include "MadgwickAHRS/MadgwickAHRS.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

SemaphoreHandle_t imu_sensor_handler_semaphore;

static imu_sensor_t* imu_sensor_handler = NULL;
static imu_mode_t imu_mode = IMU_SERVICE_WITHOUT_MAG;
static vec3f euler_angle = {0.0f, 0.0f, 0.0f};
static TaskHandle_t imu_service_task_handle = NULL;

exit_code_t imu_service_init(imu_sensor_t* imu_sensor) {
    if (imu_sensor == NULL || imu_sensor->imu_init == NULL || imu_sensor->imu_get_acc == NULL ||
        imu_sensor->imu_get_gyro == NULL || imu_sensor->imu_get_mag == NULL) return EXIT_INVALID_PARAM;
    imu_sensor_handler = imu_sensor;
    imu_sensor_handler->imu_init();
    imu_sensor_handler->is_initialized = true;
    imu_sensor_handler_semaphore = xSemaphoreCreateMutex();
    return EXIT_OK;
}
void imu_service_task(void* dt) {
    if (imu_sensor_handler == NULL) return;
    TickType_t ticks = xTaskGetTickCount();
    while (1) {

        vec3f acc, gyro, mag;
        xSemaphoreTake(imu_sensor_handler_semaphore, portMAX_DELAY);
        imu_sensor_handler->imu_get_acc(&acc);
        imu_sensor_handler->imu_get_gyro(&gyro);
        imu_sensor_handler->imu_get_mag(&mag);
        xSemaphoreGive(imu_sensor_handler_semaphore);

        switch (imu_mode)
        {
            case IMU_SERVICE_WITHOUT_MAG:
                MadgwickAHRSupdateIMU(gyro.x, gyro.y, gyro.z, acc.x, acc.y, acc.z);
                break;
            case IMU_SERVICE_WITH_MAG:
                MadgwickAHRSupdate(gyro.x, gyro.y, gyro.z, acc.x, acc.y, acc.z, mag.x, mag.y, mag.z);
                break;
        }

        vTaskDelayUntil(&ticks, 5);
    }

}

exit_code_t imu_service_start() {
    if (imu_sensor_handler == NULL) return EXIT_NOT_INITIALIZED;
    const BaseType_t xTaskCreateStatus = xTaskCreate(imu_service_task, "IMU_Service", configMINIMAL_STACK_SIZE, NULL, 1, &imu_service_task_handle);
    if (xTaskCreateStatus != pdPASS) return EXIT_FAIL;
    return EXIT_OK;
}

exit_code_t imu_service_get_euler(vec3f* euler) {
    if (imu_sensor_handler == NULL) return EXIT_NOT_INITIALIZED;
    MadgwickAHRS_getEuler(&euler->x, &euler->y, &euler->z);
    return EXIT_OK;
}
exit_code_t imu_service_get_sensor(vec3f* acc, vec3f* gyro, vec3f* mag) {
    if (imu_sensor_handler == NULL) return EXIT_NOT_INITIALIZED;
    xSemaphoreTake(imu_sensor_handler_semaphore, portMAX_DELAY);
    imu_sensor_handler->imu_get_acc(acc);
    imu_sensor_handler->imu_get_gyro(gyro);
    imu_sensor_handler->imu_get_mag(mag);
    xSemaphoreGive(imu_sensor_handler_semaphore);
    return EXIT_OK;
}
exit_code_t imu_service_deinit() {
    if (imu_sensor_handler == NULL || imu_sensor_handler->imu_deinit == NULL) return EXIT_NOT_INITIALIZED;
    vTaskDelete(imu_service_task_handle);
    xSemaphoreTake(imu_sensor_handler_semaphore, portMAX_DELAY);
    imu_sensor_handler->imu_deinit();
    xSemaphoreGive(imu_sensor_handler_semaphore);
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