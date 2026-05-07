#include "imu963ra.h"
#include "spi.h"
#include "main.h"
#include "cmsis_os.h"

/* ── IMU963RA register map ─────────────────────────────────────────── */

#define IMU963RA_SPI_W              (0x00)
#define IMU963RA_SPI_R              (0x80)

#define IMU963RA_FUNC_CFG_ACCESS    (0x01)
#define IMU963RA_INT1_CTRL          (0x0D)
#define IMU963RA_WHO_AM_I           (0x0F)
#define IMU963RA_CTRL1_XL           (0x10)
#define IMU963RA_CTRL2_G            (0x11)
#define IMU963RA_CTRL3_C            (0x12)
#define IMU963RA_CTRL4_C            (0x13)
#define IMU963RA_CTRL5_C            (0x14)
#define IMU963RA_CTRL6_C            (0x15)
#define IMU963RA_CTRL7_G            (0x16)
#define IMU963RA_CTRL9_XL           (0x18)
#define IMU963RA_OUTX_L_G           (0x22)
#define IMU963RA_OUTX_L_A           (0x28)
#define IMU963RA_STATUS_MASTER      (0x22)  /* sensor hub page */

/* Sensor hub registers (accessed via FUNC_CFG_ACCESS=0x40) */
#define IMU963RA_SENSOR_HUB_1       (0x02)
#define IMU963RA_MASTER_CONFIG      (0x14)
#define IMU963RA_SLV0_ADD           (0x15)
#define IMU963RA_SLV0_SUBADD        (0x16)
#define IMU963RA_SLV0_CONFIG        (0x17)
#define IMU963RA_DATAWRITE_SLV0     (0x21)

/* Magnetometer (I2C address 7-bit = 0x0D) */
#define IMU963RA_MAG_ADDR           (0x0D)
#define IMU963RA_MAG_OUTX_L         (0x00)
#define IMU963RA_MAG_CONTROL1       (0x09)
#define IMU963RA_MAG_CONTROL2       (0x0A)
#define IMU963RA_MAG_FBR            (0x0B)
#define IMU963RA_MAG_CHIP_ID        (0x0D)

/* ── Configuration constants ───────────────────────────────────────── */

#define IMU963RA_TIMEOUT_COUNT      (0x00FF)

/* Default ranges: ±8G acc, ±2000DPS gyro, 8G mag */
#define ACC_CTRL1_VAL               (0x8C)
#define ACC_DIVISOR                 (4098.0f)

#define GYRO_CTRL2_VAL              (0x8C)
#define GYRO_DPS_DIVISOR            (14.3f)
#define DEG_TO_RAD                  (0.017453292519943295f)

#define MAG_CONTROL1_VAL            (0x19)
#define MAG_DIVISOR                 (3000.0f)

/* ── HAL SPI helpers ───────────────────────────────────────────────── */

static inline void cs_low(void)
{
    HAL_GPIO_WritePin(SPI2_CS_GPIO_Port, SPI2_CS_Pin, GPIO_PIN_RESET);
}

static inline void cs_high(void)
{
    HAL_GPIO_WritePin(SPI2_CS_GPIO_Port, SPI2_CS_Pin, GPIO_PIN_SET);
}

static void spi_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t tx[2] = { reg | IMU963RA_SPI_W, data };
    cs_low();
    HAL_SPI_Transmit(&hspi2, tx, 2, HAL_MAX_DELAY);
    cs_high();
}

static uint8_t spi_read_reg(uint8_t reg)
{
    uint8_t tx = reg | IMU963RA_SPI_R;
    uint8_t rx = 0;
    cs_low();
    HAL_SPI_Transmit(&hspi2, &tx, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi2, &rx, 1, HAL_MAX_DELAY);
    cs_high();
    return rx;
}

static void spi_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t tx = reg | IMU963RA_SPI_R;
    cs_low();
    HAL_SPI_Transmit(&hspi2, &tx, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi2, buf, len, HAL_MAX_DELAY);
    cs_high();
}

/* ── Sensor hub helpers (magnetometer via internal I2C master) ─────── */

static uint8_t imu_write_mag_reg(uint8_t addr, uint8_t reg, uint8_t data)
{
    uint8_t addr_8bit = addr << 1;
    spi_write_reg(IMU963RA_SLV0_CONFIG, 0x00);
    spi_write_reg(IMU963RA_SLV0_ADD, addr_8bit | 0);
    spi_write_reg(IMU963RA_SLV0_SUBADD, reg);
    spi_write_reg(IMU963RA_DATAWRITE_SLV0, data);
    spi_write_reg(IMU963RA_MASTER_CONFIG, 0x4C);

    for (uint16_t t = 0; t < IMU963RA_TIMEOUT_COUNT; t++) {
        if (spi_read_reg(IMU963RA_STATUS_MASTER) & 0x80)
            return 0;
        osDelay(2);
    }
    return 1;
}

static uint8_t imu_read_mag_reg(uint8_t addr, uint8_t reg)
{
    uint8_t addr_8bit = addr << 1;
    spi_write_reg(IMU963RA_SLV0_ADD, addr_8bit | 1);
    spi_write_reg(IMU963RA_SLV0_SUBADD, reg);
    spi_write_reg(IMU963RA_SLV0_CONFIG, 0x01);
    spi_write_reg(IMU963RA_MASTER_CONFIG, 0x4C);

    for (uint16_t t = 0; t < IMU963RA_TIMEOUT_COUNT; t++) {
        if (spi_read_reg(IMU963RA_STATUS_MASTER) & 0x01)
            break;
        osDelay(2);
    }
    return spi_read_reg(IMU963RA_SENSOR_HUB_1);
}

static void imu_connect_mag(uint8_t addr, uint8_t reg)
{
    uint8_t addr_8bit = addr << 1;
    spi_write_reg(IMU963RA_SLV0_ADD, addr_8bit | 1);
    spi_write_reg(IMU963RA_SLV0_SUBADD, reg);
    spi_write_reg(IMU963RA_SLV0_CONFIG, 0x06);
    spi_write_reg(IMU963RA_MASTER_CONFIG, 0x6C);
}

/* ── Self-check ────────────────────────────────────────────────────── */

static exit_code_t acc_gyro_self_check(void)
{
    for (uint16_t t = 0; t < IMU963RA_TIMEOUT_COUNT; t++) {
        if (spi_read_reg(IMU963RA_WHO_AM_I) == 0x6B)
            return EXIT_OK;
        osDelay(10);
    }
    return EXIT_HW_FAILURE;
}

static exit_code_t mag_self_check(void)
{
    for (uint16_t t = 0; t < IMU963RA_TIMEOUT_COUNT; t++) {
        if (imu_read_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_CHIP_ID) == 0xFF)
            return EXIT_OK;
        osDelay(10);
    }
    return EXIT_HW_FAILURE;
}

/* ── Public API (imu_sensor_t vtable functions) ────────────────────── */

static bool initialized = false;

exit_code_t imu963ra_init(void)
{
    osDelay(10);

    do {
        spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00);
        spi_write_reg(IMU963RA_CTRL3_C, 0x01);       /* reset */
        osDelay(2);
        spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00);

        if (acc_gyro_self_check() != EXIT_OK)
            break;

        spi_write_reg(IMU963RA_INT1_CTRL, 0x03);     /* data-ready interrupt */

        /* Accelerometer: ±8G, 1.66kHz ODR */
        spi_write_reg(IMU963RA_CTRL1_XL, ACC_CTRL1_VAL);

        /* Gyroscope: ±2000DPS, 1.66kHz ODR */
        spi_write_reg(IMU963RA_CTRL2_G, GYRO_CTRL2_VAL);

        /* DLPF and mode config */
        spi_write_reg(IMU963RA_CTRL3_C, 0x44);
        spi_write_reg(IMU963RA_CTRL4_C, 0x02);
        spi_write_reg(IMU963RA_CTRL5_C, 0x00);
        spi_write_reg(IMU963RA_CTRL6_C, 0x00);
        spi_write_reg(IMU963RA_CTRL7_G, 0x00);
        spi_write_reg(IMU963RA_CTRL9_XL, 0x01);     /* disable I3C */

        /* Sensor hub: configure magnetometer */
        spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x40);
        spi_write_reg(IMU963RA_MASTER_CONFIG, 0x80); /* reset I2C master */
        osDelay(2);
        spi_write_reg(IMU963RA_MASTER_CONFIG, 0x00);
        osDelay(2);

        imu_write_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL2, 0x80);
        osDelay(2);
        imu_write_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL2, 0x00);
        osDelay(2);

        if (mag_self_check() != EXIT_OK)
            break;

        /* Magnetometer: 8G range */
        imu_write_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL1, MAG_CONTROL1_VAL);
        imu_write_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_FBR, 0x01);
        imu_connect_mag(IMU963RA_MAG_ADDR, IMU963RA_MAG_OUTX_L);

        spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00);
        osDelay(20);

        initialized = true;
        return EXIT_OK;
    } while (0);

    return EXIT_HW_FAILURE;
}

exit_code_t imu963ra_deinit(void)
{
    initialized = false;
    return EXIT_OK;
}

exit_code_t imu963ra_read_acc(vec3f *acc)
{
    if (!initialized || acc == NULL) return EXIT_NOT_INITIALIZED;

    uint8_t dat[6];
    spi_read_regs(IMU963RA_OUTX_L_A, dat, 6);

    int16_t raw_x = (int16_t)((uint16_t)dat[1] << 8 | dat[0]);
    int16_t raw_y = (int16_t)((uint16_t)dat[3] << 8 | dat[2]);
    int16_t raw_z = (int16_t)((uint16_t)dat[5] << 8 | dat[4]);

    acc->x = (float)raw_x / ACC_DIVISOR;
    acc->y = (float)raw_y / ACC_DIVISOR;
    acc->z = (float)raw_z / ACC_DIVISOR;
    return EXIT_OK;
}

exit_code_t imu963ra_read_gyro(vec3f *gyro)
{
    if (!initialized || gyro == NULL) return EXIT_NOT_INITIALIZED;

    uint8_t dat[6];
    spi_read_regs(IMU963RA_OUTX_L_G, dat, 6);

    int16_t raw_x = (int16_t)((uint16_t)dat[1] << 8 | dat[0]);
    int16_t raw_y = (int16_t)((uint16_t)dat[3] << 8 | dat[2]);
    int16_t raw_z = (int16_t)((uint16_t)dat[5] << 8 | dat[4]);

    /* Convert: raw → deg/s → rad/s */
    gyro->x = ((float)raw_x / GYRO_DPS_DIVISOR) * DEG_TO_RAD;
    gyro->y = ((float)raw_y / GYRO_DPS_DIVISOR) * DEG_TO_RAD;
    gyro->z = ((float)raw_z / GYRO_DPS_DIVISOR) * DEG_TO_RAD;
    return EXIT_OK;
}

exit_code_t imu963ra_read_mag(vec3f *mag)
{
    if (!initialized || mag == NULL) return EXIT_NOT_INITIALIZED;

    uint8_t dat[6];

    spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x40);
    uint8_t status = spi_read_reg(IMU963RA_STATUS_MASTER);
    if (status & 0x01) {
        spi_read_regs(IMU963RA_SENSOR_HUB_1, dat, 6);

        int16_t raw_x = (int16_t)((uint16_t)dat[1] << 8 | dat[0]);
        int16_t raw_y = (int16_t)((uint16_t)dat[3] << 8 | dat[2]);
        int16_t raw_z = (int16_t)((uint16_t)dat[5] << 8 | dat[4]);

        mag->x = (float)raw_x / MAG_DIVISOR;
        mag->y = (float)raw_y / MAG_DIVISOR;
        mag->z = (float)raw_z / MAG_DIVISOR;
    }
    spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00);
    return EXIT_OK;
}
