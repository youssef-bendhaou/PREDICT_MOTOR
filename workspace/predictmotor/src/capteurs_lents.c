#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "capteurs_lents.h"

LOG_MODULE_REGISTER(lents, LOG_LEVEL_INF);

#define PERIODE_MS 2000   /* une mesure toutes les 2 s suffit */
#define PRIORITE   7      /* plus le nombre est grand, moins c'est prioritaire */
#define TAILLE_PILE 2048

static const struct device *const ina = DEVICE_DT_GET(DT_NODELABEL(ina219));
static const struct device *const ds  = DEVICE_DT_GET(DT_NODELABEL(ds18b20));

/* La "boite aux lettres" partagee, protegee par un verrou (mutex) */
static struct mesures_lentes dernieres;
K_MUTEX_DEFINE(verrou);

K_THREAD_STACK_DEFINE(pile, TAILLE_PILE);
static struct k_thread fil;

static void boucle(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    bool ina_ok = device_is_ready(ina);
    bool ds_ok = device_is_ready(ds);

    if (!ina_ok) {
        LOG_WRN("INA219 absent : courant non mesure");
    }
    if (!ds_ok) {
        LOG_WRN("DS18B20 absent : temperature non mesuree");
    }

    while (1) {
        struct mesures_lentes m = {0};
        struct sensor_value v;

        if (ina_ok && sensor_sample_fetch(ina) == 0) {
            sensor_channel_get(ina, SENSOR_CHAN_CURRENT, &v);
            m.courant_a = (float)sensor_value_to_double(&v);
            sensor_channel_get(ina, SENSOR_CHAN_VOLTAGE, &v);
            m.tension_v = (float)sensor_value_to_double(&v);
            m.courant_ok = true;
        }

        /* Lent (750 ms) : c'est pour ca qu'on est dans un thread a part */
        if (ds_ok && sensor_sample_fetch(ds) == 0) {
            sensor_channel_get(ds, SENSOR_CHAN_AMBIENT_TEMP, &v);
            m.temperature_c = (float)sensor_value_to_double(&v);
            m.temperature_ok = true;
        }

        k_mutex_lock(&verrou, K_FOREVER);
        dernieres = m;
        k_mutex_unlock(&verrou);

        k_msleep(PERIODE_MS);
    }
}

void capteurs_lents_demarrer(void)
{
    k_thread_create(&fil, pile, K_THREAD_STACK_SIZEOF(pile),
                    boucle, NULL, NULL, NULL,
                    PRIORITE, 0, K_NO_WAIT);
}

void capteurs_lents_lire(struct mesures_lentes *m)
{
    k_mutex_lock(&verrou, K_FOREVER);
    *m = dernieres;
    k_mutex_unlock(&verrou);
}
