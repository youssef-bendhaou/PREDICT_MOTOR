#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include "vibration.h"

static const struct device *const accel = DEVICE_DT_GET_ONE(adi_adxl345);

int vibration_init(void)
{
    return device_is_ready(accel) ? 0 : -ENODEV;
}

int vibration_lire(float xyz[3])
{
    struct sensor_value v[3];
    int ret = sensor_sample_fetch(accel);

    if (ret < 0) {
        return ret;
    }
    sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v);
    for (int i = 0; i < 3; i++) {
        xyz[i] = (float)sensor_value_to_double(&v[i]);
    }
    return 0;
}
