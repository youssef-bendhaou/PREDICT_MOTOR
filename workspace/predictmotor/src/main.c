#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(acq, LOG_LEVEL_INF);

#define PERIODE_MS 10   /* 100 Hz */

static const struct device *const accel = DEVICE_DT_GET_ONE(adi_adxl345);

/* Le timer "tape" le semaphore a intervalle regulier */
K_SEM_DEFINE(tick_sem, 0, 1);

static void tick_handler(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_sem_give(&tick_sem);
}
K_TIMER_DEFINE(tick_timer, tick_handler, NULL);

int main(void)
{
    struct sensor_value v[3];
    uint32_t n = 0;

    printk("PredictMotor demarre\n");

    if (!device_is_ready(accel)) {
        LOG_ERR("ADXL345 non pret : verifier cablage et overlay");
        return 0;
    }
    LOG_INF("ADXL345 OK, acquisition a %d Hz", 1000 / PERIODE_MS);

    k_timer_start(&tick_timer, K_MSEC(PERIODE_MS), K_MSEC(PERIODE_MS));

    while (1) {
        k_sem_take(&tick_sem, K_FOREVER);

        if (sensor_sample_fetch(accel) < 0) {
            LOG_WRN("echec de lecture");
            continue;
        }
        sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, v);

        /* On n'affiche qu'un echantillon sur 10 pour ne pas saturer l'UART */
        if (++n % 10 == 0) {
            printk("t=%u ms  x=%.2f  y=%.2f  z=%.2f m/s2\n",
                   k_uptime_get_32(),
                   sensor_value_to_double(&v[0]),
                   sensor_value_to_double(&v[1]),
                   sensor_value_to_double(&v[2]));
        }
    }
    return 0;
}
