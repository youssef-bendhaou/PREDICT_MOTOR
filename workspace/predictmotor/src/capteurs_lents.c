#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "capteurs_lents.h"
#include "pzem.h"

LOG_MODULE_REGISTER(lents, LOG_LEVEL_INF);

#define PERIODE_MS 2000 /* une mesure toutes les 2 s suffit */
#define PRIORITE 7      /* plus le nombre est grand, moins c'est prioritaire */
#define TAILLE_PILE 2048

static const struct device *const ds = DEVICE_DT_GET(DT_NODELABEL(ds18b20));

/* La "boite aux lettres" partagee, protegee par un verrou (mutex) */
static struct mesures_lentes dernieres;
K_MUTEX_DEFINE(verrou);

K_THREAD_STACK_DEFINE(pile, TAILLE_PILE);
static struct k_thread fil;

/* PZEM-004T : remplit les champs electriques de m. Retourne 0 si OK. */
static int lire_pzem(struct mesures_lentes *m)
{
    struct pzem_mesure pz;
    int err = pzem_lire(&pz);

    if (err)
    {
        return err;
    }

    m->tension_v = (float)pz.tension_dv / 10.0f;
    m->courant_a = (float)pz.courant_ua / 1000000.0f;
    m->puissance_w = (float)pz.puissance_mw / 1000.0f;
    m->facteur_p = (float)pz.facteur_pc / 100.0f;
    m->frequence_hz = (float)pz.frequence_dhz / 10.0f;
    m->energie_wh = pz.energie_wh;

    if (pz.alarme)
    {
        LOG_WRN("PZEM : alarme de surpuissance");
    }
    return 0;
}

static void boucle(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    bool pzem_ok = (pzem_init() == 0);
    bool ds_ok = device_is_ready(ds);
    bool pzem_repondait = true; /* pour ne prevenir qu'au changement */

    if (!pzem_ok)
    {
        LOG_WRN("UART2 du PZEM non pret : courant non mesure");
    }
    if (!ds_ok)
    {
        LOG_WRN("DS18B20 absent : temperature non mesuree");
    }

    while (1)
    {
        struct mesures_lentes m = {0};
        struct sensor_value v;

        if (pzem_ok)
        {
            int err = lire_pzem(&m);

            m.courant_ok = (err == 0);
            if (err && pzem_repondait)
            {
                LOG_WRN("PZEM ne repond pas (%d) : 230 V sur L/N ? RX/TX croises ?", err);
            }
            else if (!err && !pzem_repondait)
            {
                LOG_INF("PZEM de nouveau en ligne");
            }
            pzem_repondait = (err == 0);
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