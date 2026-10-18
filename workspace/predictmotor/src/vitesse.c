#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include "vitesse.h"

static const struct device *const pcnt = DEVICE_DT_GET(DT_NODELABEL(pcnt));

static int16_t compte_prec;
static int64_t t_prec;

static int lire_compteur(int16_t *compte)
{
    struct sensor_value v;
    int ret = sensor_sample_fetch(pcnt);

    if (ret < 0) {
        return ret;
    }
    ret = sensor_channel_get(pcnt, SENSOR_CHAN_ROTATION, &v);
    *compte = (int16_t)v.val1;
    return ret;
}

int vitesse_init(void)
{
    if (!device_is_ready(pcnt)) {
        return -ENODEV;
    }
    t_prec = k_uptime_get();
    return lire_compteur(&compte_prec);
}

int vitesse_lire(float *tr_min)
{
    int16_t compte;
    int ret = lire_compteur(&compte);

    if (ret < 0) {
        return ret;
    }
    int64_t t = k_uptime_get();
    /* Le compteur materiel fait 16 bits : la soustraction en int16_t
     * donne le bon resultat meme quand il deborde */
    int16_t delta = (int16_t)(compte - compte_prec);
    int64_t dt_ms = t - t_prec;

    compte_prec = compte;
    t_prec = t;
    if (dt_ms <= 0) {
        return -EAGAIN;
    }
    *tr_min = (float)delta * 60000.0f /
              ((float)dt_ms * (float)CONFIG_PM_ENC_IMPULSIONS_PAR_TOUR);
    return 0;
}
