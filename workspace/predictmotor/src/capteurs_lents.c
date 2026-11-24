#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "capteurs_lents.h"

LOG_MODULE_REGISTER(lents, LOG_LEVEL_INF);

#define PERIODE_MS 2000 /* une mesure toutes les 2 s suffit */
#define PRIORITE 7      /* plus le nombre est grand, moins c'est prioritaire */
#define TAILLE_PILE 2048

/* Courant : 400 echantillons a 2 kHz = 200 ms = 10 periodes du 50 Hz */
#define CT_N 400
#define CT_PERIODE_US 500

static const struct adc_dt_spec ct = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static const struct device *const ds = DEVICE_DT_GET(DT_NODELABEL(ds18b20));

/* La "boite aux lettres" partagee, protegee par un verrou (mutex) */
static struct mesures_lentes dernieres;
K_MUTEX_DEFINE(verrou);

K_THREAD_STACK_DEFINE(pile, TAILLE_PILE);
static struct k_thread fil;

/* TA12-100 : valeur efficace du courant du ventilateur */
static int lire_courant(float *courant_a, float *decalage_mv)
{
    int16_t brut;
    struct adc_sequence seq = {
        .buffer = &brut,
        .buffer_size = sizeof(brut),
    };
    float somme = 0.0f, somme2 = 0.0f;
    int64_t t0 = k_uptime_ticks();

    adc_sequence_init_dt(&ct, &seq);

    for (uint32_t i = 0; i < CT_N; i++)
    {
        if (adc_read_dt(&ct, &seq) < 0)
        {
            return -EIO;
        }
        int32_t mv = brut;

        adc_raw_to_millivolts_dt(&ct, &mv);
        somme += (float)mv;
        somme2 += (float)mv * (float)mv;

        /* Echantillons reguliers : on attend l'instant t0 + (i+1) x 0,5 ms */
        k_sleep(K_TIMEOUT_ABS_TICKS(t0 + k_us_to_ticks_ceil64((uint64_t)(i + 1) * CT_PERIODE_US)));
    }

    float moyenne = somme / CT_N;
    float variance = somme2 / CT_N - moyenne * moyenne;
    float v_eff_mv = variance > 0.0f ? sqrtf(variance) : 0.0f;

    /* mV / ohm = mA au secondaire ; x rapport ; / nombre de passages ; /1000 -> A */
    *courant_a = v_eff_mv / (float)CONFIG_PM_CT_CHARGE_OHMS *
                 (float)CONFIG_PM_CT_RAPPORT / (float)CONFIG_PM_CT_TOURS / 1000.0f;
    *decalage_mv = moyenne;
    return 0;
}

static void boucle(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    bool ct_ok = adc_is_ready_dt(&ct) && adc_channel_setup_dt(&ct) == 0;
    bool ds_ok = device_is_ready(ds);

    if (!ct_ok)
    {
        LOG_WRN("ADC du TA12-100 non pret : courant non mesure");
    }
    if (!ds_ok)
    {
        LOG_WRN("DS18B20 absent : temperature non mesuree");
    }

    while (1)
    {
        struct mesures_lentes m = {0};
        struct sensor_value v;

        if (ct_ok && lire_courant(&m.courant_a, &m.decalage_mv) == 0)
        {
            m.courant_ok = true;
        }

        /* Lent (750 ms) : c'est pour ca qu'on est dans un thread a part */
        if (ds_ok && sensor_sample_fetch(ds) == 0)
        {
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