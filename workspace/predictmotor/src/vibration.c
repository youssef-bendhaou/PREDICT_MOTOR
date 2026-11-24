#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include "vibration.h"

LOG_MODULE_REGISTER(vibration, LOG_LEVEL_INF);

static const struct device *const accel = DEVICE_DT_GET_ONE(adi_adxl345);

/* Acces direct au capteur sur le meme bus SPI que le pilote (mode 3) */
static const struct spi_dt_spec bus = SPI_DT_SPEC_GET(DT_NODELABEL(adxl345),
                                                      SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA, 0);

#define REG_DATA_FORMAT 0x31
#define PLAGE_2G 0x00 /* +/-2 g, 10 bits : 3,9 mg par pas */
#define LECTURE 0x80

/* Le pilote Zephyr regle +/-16 g (31,2 mg par pas) et convertit avec cette
 * echelle. En +/-2 g un pas vaut 8 fois moins : on divise par 8. */
#define ECHELLE (1.0f / 8.0f)

static int ecrire_registre(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = {reg, val};
    const struct spi_buf b = {.buf = tx, .len = sizeof(tx)};
    const struct spi_buf_set s = {.buffers = &b, .count = 1};

    return spi_write_dt(&bus, &s);
}

static int lire_registre(uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = {reg | LECTURE, 0};
    uint8_t rx[2];
    const struct spi_buf bt = {.buf = tx, .len = sizeof(tx)};
    const struct spi_buf br = {.buf = rx, .len = sizeof(rx)};
    const struct spi_buf_set st = {.buffers = &bt, .count = 1};
    const struct spi_buf_set sr = {.buffers = &br, .count = 1};
    int ret = spi_transceive_dt(&bus, &st, &sr);

    *val = rx[1];
    return ret;
}

int vibration_init(void)
{
    uint8_t format = 0xFF;

    if (!device_is_ready(accel))
    {
        return -ENODEV;
    }
    /* Passage en +/-2 g pour une resolution 8 fois plus fine */
    if (ecrire_registre(REG_DATA_FORMAT, PLAGE_2G) < 0 ||
        lire_registre(REG_DATA_FORMAT, &format) < 0 || format != PLAGE_2G)
    {
        LOG_ERR("ADXL345 : reglage +/-2 g impossible (lu 0x%02x)", format);
        return -EIO;
    }
    LOG_INF("ADXL345 regle en +/-2 g (pas de 0,038 m/s2)");
    return 0;
}

int vibration_lire(float xyz[3])
{
    struct sensor_value v[3];
    int ret = sensor_sample_fetch(accel);

    if (ret < 0)
    {
        return ret;
    }
    sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v);
    for (int i = 0; i < 3; i++)
    {
        xyz[i] = (float)sensor_value_to_double(&v[i]) * ECHELLE;
    }
    return 0;
}