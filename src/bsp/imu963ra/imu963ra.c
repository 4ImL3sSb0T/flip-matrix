#include "imu963ra.h"
#include "spi.h"
#include "main.h"
#include "cmsis_os.h"

#define EXIT_IF_ERR(expr)  do { ec = (expr); if (ec != EXIT_OK) return ec; } while (0)

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

#define IMU963RA_TIMEOUT_COUNT      (100)

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
    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_RESET);
}

static inline void cs_high(void)
{
    HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
}

static exit_code_t spi_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t tx[2] = { reg | IMU963RA_SPI_W, data };
    cs_low();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi6, tx, 2, HAL_MAX_DELAY);
    cs_high();
    return (st == HAL_OK) ? EXIT_OK : EXIT_HW_FAILURE;
}

static exit_code_t spi_read_reg(uint8_t reg, uint8_t *val)
{
    uint8_t tx = reg | IMU963RA_SPI_R;
    uint8_t rx = 0;
    cs_low();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi6, &tx, 1, HAL_MAX_DELAY);
    if (st != HAL_OK) { cs_high(); return EXIT_HW_FAILURE; }
    st = HAL_SPI_Receive(&hspi6, &rx, 1, HAL_MAX_DELAY);
    cs_high();
    if (st != HAL_OK) return EXIT_HW_FAILURE;
    *val = rx;
    return EXIT_OK;
}

static exit_code_t spi_read_regs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t tx = reg | IMU963RA_SPI_R;
    cs_low();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi6, &tx, 1, HAL_MAX_DELAY);
    if (st != HAL_OK) { cs_high(); return EXIT_HW_FAILURE; }
    st = HAL_SPI_Receive(&hspi6, buf, len, HAL_MAX_DELAY);
    cs_high();
    return (st == HAL_OK) ? EXIT_OK : EXIT_HW_FAILURE;
}

/* ── Sensor hub helpers (magnetometer via internal I2C master) ─────── */

static exit_code_t imu_write_mag_reg(uint8_t addr, uint8_t reg, uint8_t data)
{
    exit_code_t ec;
    uint8_t addr_8bit = addr << 1;
    ec = spi_write_reg(IMU963RA_SLV0_CONFIG, 0x00);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_SLV0_ADD, addr_8bit | 0);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_SLV0_SUBADD, reg);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_DATAWRITE_SLV0, data);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_MASTER_CONFIG, 0x4C);
    if (ec != EXIT_OK) return ec;

    for (uint16_t t = 0; t < IMU963RA_TIMEOUT_COUNT; t++) {
        uint8_t status;
        ec = spi_read_reg(IMU963RA_STATUS_MASTER, &status);
        if (ec != EXIT_OK) return ec;
        if (status & 0x80)
            return EXIT_OK;
        osDelay(2);
    }
    return EXIT_TIMEOUT;
}

static exit_code_t imu_read_mag_reg(uint8_t addr, uint8_t reg, uint8_t *val)
{
    exit_code_t ec;
    uint8_t addr_8bit = addr << 1;
    ec = spi_write_reg(IMU963RA_SLV0_ADD, addr_8bit | 1);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_SLV0_SUBADD, reg);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_SLV0_CONFIG, 0x01);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_MASTER_CONFIG, 0x4C);
    if (ec != EXIT_OK) return ec;

    for (uint16_t t = 0; t < IMU963RA_TIMEOUT_COUNT; t++) {
        uint8_t status;
        ec = spi_read_reg(IMU963RA_STATUS_MASTER, &status);
        if (ec != EXIT_OK) return ec;
        if (status & 0x01)
            return spi_read_reg(IMU963RA_SENSOR_HUB_1, val);
        osDelay(2);
    }
    return EXIT_TIMEOUT;
}

static exit_code_t imu_connect_mag(uint8_t addr, uint8_t reg)
{
    exit_code_t ec;
    uint8_t addr_8bit = addr << 1;
    ec = spi_write_reg(IMU963RA_SLV0_ADD, addr_8bit | 1);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_SLV0_SUBADD, reg);
    if (ec != EXIT_OK) return ec;
    ec = spi_write_reg(IMU963RA_SLV0_CONFIG, 0x06);
    if (ec != EXIT_OK) return ec;
    return spi_write_reg(IMU963RA_MASTER_CONFIG, 0x6C);
}

/* ── Self-check ────────────────────────────────────────────────────── */

static exit_code_t acc_gyro_self_check(void)
{
    for (uint16_t t = 0; t < IMU963RA_TIMEOUT_COUNT; t++) {
        uint8_t id;
        if (spi_read_reg(IMU963RA_WHO_AM_I, &id) == EXIT_OK && id == 0x6B)
            return EXIT_OK;
        osDelay(10);
    }
    return EXIT_HW_FAILURE;
}

static exit_code_t mag_self_check(void)
{
    for (uint16_t t = 0; t < IMU963RA_TIMEOUT_COUNT; t++) {
        uint8_t id;
        if (imu_read_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_CHIP_ID, &id) == EXIT_OK && id == 0xFF)
            return EXIT_OK;
        osDelay(10);
    }
    return EXIT_HW_FAILURE;
}

/* ── Public API (imu_sensor_t vtable functions) ────────────────────── */

static bool initialized = false;

exit_code_t imu963ra_init(void)
{
    exit_code_t ec;
    osDelay(10);

    do {
        EXIT_IF_ERR(spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00));
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL3_C, 0x01));       /* reset */
        osDelay(2);
        EXIT_IF_ERR(spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00));

        if (acc_gyro_self_check() != EXIT_OK)
            break;

        EXIT_IF_ERR(spi_write_reg(IMU963RA_INT1_CTRL, 0x03));     /* data-ready interrupt */

        /* Accelerometer: ±8G, 1.66kHz ODR */
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL1_XL, ACC_CTRL1_VAL));

        /* Gyroscope: ±2000DPS, 1.66kHz ODR */
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL2_G, GYRO_CTRL2_VAL));

        /* DLPF and mode config */
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL3_C, 0x44));
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL4_C, 0x02));
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL5_C, 0x00));
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL6_C, 0x00));
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL7_G, 0x00));
        EXIT_IF_ERR(spi_write_reg(IMU963RA_CTRL9_XL, 0x01));     /* disable I3C */

        /* Sensor hub: configure magnetometer */
        EXIT_IF_ERR(spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x40));
        EXIT_IF_ERR(spi_write_reg(IMU963RA_MASTER_CONFIG, 0x80)); /* reset I2C master */
        osDelay(2);
        EXIT_IF_ERR(spi_write_reg(IMU963RA_MASTER_CONFIG, 0x00));
        osDelay(2);

        EXIT_IF_ERR(imu_write_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL2, 0x80));
        osDelay(2);
        EXIT_IF_ERR(imu_write_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL2, 0x00));
        osDelay(2);

        if (mag_self_check() != EXIT_OK)
            break;

        /* Magnetometer: 8G range */
        EXIT_IF_ERR(imu_write_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL1, MAG_CONTROL1_VAL));
        EXIT_IF_ERR(imu_write_mag_reg(IMU963RA_MAG_ADDR, IMU963RA_MAG_FBR, 0x01));
        EXIT_IF_ERR(imu_connect_mag(IMU963RA_MAG_ADDR, IMU963RA_MAG_OUTX_L));

        EXIT_IF_ERR(spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00));
        osDelay(20);

        initialized = true;
        return EXIT_OK;
    } while (0);

    return EXIT_HW_FAILURE;
}

exit_code_t imu963ra_deinit(void)
{
    exit_code_t ec;

    /* Software reset the sensor */
    ec = spi_write_reg(IMU963RA_CTRL3_C, 0x01);

    /* Shut down sensor hub */
    spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x40);
    spi_write_reg(IMU963RA_MASTER_CONFIG, 0x00);
    spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00);

    initialized = false;
    return (ec != EXIT_OK) ? ec : EXIT_OK;
}

exit_code_t imu963ra_read_acc(vec3f *acc)
{
    if (!initialized || acc == NULL) return EXIT_NOT_INITIALIZED;

    uint8_t dat[6];
    exit_code_t ec = spi_read_regs(IMU963RA_OUTX_L_A, dat, 6);
    if (ec != EXIT_OK) return ec;

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
    exit_code_t ec = spi_read_regs(IMU963RA_OUTX_L_G, dat, 6);
    if (ec != EXIT_OK) return ec;

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
    exit_code_t ec;

    ec = spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x40);
    if (ec != EXIT_OK) return ec;

    uint8_t status;
    ec = spi_read_reg(IMU963RA_STATUS_MASTER, &status);
    if (ec != EXIT_OK) { spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00); return ec; }

    if (status & 0x01) {
        ec = spi_read_regs(IMU963RA_SENSOR_HUB_1, dat, 6);
        if (ec != EXIT_OK) { spi_write_reg(IMU963RA_FUNC_CFG_ACCESS, 0x00); return ec; }

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
