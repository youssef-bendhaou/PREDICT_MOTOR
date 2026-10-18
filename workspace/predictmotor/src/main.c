#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "vibration.h"
#include "moteur.h"
#include "vitesse.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define FE_HZ      100               /* echantillons par seconde */
#define PERIODE_MS (1000 / FE_HZ)
#define FENETRE    FE_HZ             /* bilan chaque seconde */

/* Le timer "tape" le semaphore a intervalle regulier */
K_SEM_DEFINE(tick_sem, 0, 1);

static void tick_handler(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_sem_give(&tick_sem);
}
K_TIMER_DEFINE(tick_timer, tick_handler, NULL);

struct stats {
    float min;
    float max;
};

static void stats_reset(struct stats s[3])
{
    for (int i = 0; i < 3; i++) {
        s[i].min = 1e9f;
        s[i].max = -1e9f;
    }
}

static void stats_add(struct stats s[3], const float v[3])
{
    for (int i = 0; i < 3; i++) {
        s[i].min = MIN(s[i].min, v[i]);
        s[i].max = MAX(s[i].max, v[i]);
    }
}

int main(void)
{
    float xyz[3];
    float tr_min = 0.0f;
    struct stats s[3];
    uint32_t n = 0;
    uint32_t sec = 0;

    printk("PredictMotor demarre\n");

    /* Le moteur en premier : on s'assure qu'il est arrete */
    if (moteur_init() < 0) {
        LOG_ERR("Driver moteur (BTS7960) non pret");
        return 0;
    }
    if (vibration_init() < 0) {
        LOG_ERR("ADXL345 non pret : verifier cablage SPI");
        return 0;
    }
    if (vitesse_init() < 0) {
        LOG_ERR("Encodeur non pret");
        return 0;
    }

    LOG_INF("Demarrage progressif du moteur jusqu'a %d %%", CONFIG_PM_MOTEUR_CONSIGNE);
    moteur_rampe(CONFIG_PM_MOTEUR_CONSIGNE);

    stats_reset(s);
    k_timer_start(&tick_timer, K_MSEC(PERIODE_MS), K_MSEC(PERIODE_MS));

    while (1) {
        k_sem_take(&tick_sem, K_FOREVER);

        if (vibration_lire(xyz) < 0) {
            LOG_WRN("echec de lecture ADXL345");
            continue;
        }
        stats_add(s, xyz);

        if (++n == FENETRE) {
            sec++;
            (void)vitesse_lire(&tr_min);
            printk("t=%3u s | %5.0f tr/min (f_rot=%4.1f Hz) | crete-a-crete x=%.2f y=%.2f z=%.2f m/s2\n",
                   sec,
                   (double)tr_min,
                   (double)(tr_min / 60.0f),
                   (double)(s[0].max - s[0].min),
                   (double)(s[1].max - s[1].min),
                   (double)(s[2].max - s[2].min));
            stats_reset(s);
            n = 0;
        }
    }
    return 0;
}
