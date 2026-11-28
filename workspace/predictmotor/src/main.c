#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "vibration.h"
#include "moteur.h"
#include "vitesse.h"
#include "capteurs_lents.h"
#include "reseau.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Le ventilateur tourne a ~52 Hz (3150 tr/min) : il faut echantillonner
 * a plus du double. 400 Hz = la cadence max de l'ADXL345 (odr = 5) */
#define FE_HZ 400 /* echantillons par seconde */
#define PERIODE_US (1000000 / FE_HZ)
#define FENETRE FE_HZ /* bilan chaque seconde */

/* Le timer "tape" le semaphore a intervalle regulier */
K_SEM_DEFINE(tick_sem, 0, 1);

static void tick_handler(struct k_timer *t)
{
    ARG_UNUSED(t);
    k_sem_give(&tick_sem);
}
K_TIMER_DEFINE(tick_timer, tick_handler, NULL);

struct stats
{
    float min;
    float max;
    float ref;    /* premiere mesure de la seconde */
    float somme;  /* somme des ecarts a ref */
    float somme2; /* somme des carres des ecarts a ref */
    bool vide;
};

static void stats_reset(struct stats s[3])
{
    for (int i = 0; i < 3; i++)
    {
        s[i].min = 1e9f;
        s[i].max = -1e9f;
        s[i].ref = 0.0f;
        s[i].somme = 0.0f;
        s[i].somme2 = 0.0f;
        s[i].vide = true;
    }
}

/* On travaille sur l'ecart a la premiere mesure : evite l'erreur
 * d'arrondi due a la gravite (~9,8 m/s2) dans le calcul du RMS */
static void stats_add(struct stats s[3], const float v[3])
{
    for (int i = 0; i < 3; i++)
    {
        if (s[i].vide)
        {
            s[i].ref = v[i];
            s[i].vide = false;
        }
        float e = v[i] - s[i].ref;

        s[i].min = MIN(s[i].min, v[i]);
        s[i].max = MAX(s[i].max, v[i]);
        s[i].somme += e;
        s[i].somme2 += e * e;
    }
}

/* Valeur efficace de la partie qui vibre (la gravite est retiree) */
static float stats_rms(const struct stats *s, uint32_t n)
{
    float moyenne = s->somme / (float)n;
    float variance = s->somme2 / (float)n - moyenne * moyenne;

    return variance > 0.0f ? sqrtf(variance) : 0.0f;
}

static void afficher_bilan(uint32_t sec, float tr_min, const struct stats s[3], uint32_t n)
{
    struct mesures_lentes m;

    capteurs_lents_lire(&m);

    printk("t=%3u s | moteur %s | %5.0f tr/min (f_rot=%4.1f Hz)\n",
           sec, moteur_en_marche() ? "ON " : "OFF",
           (double)tr_min, (double)(tr_min / 60.0f));

    if (n > 0)
    {
        printk("        | crete-a-crete x=%.2f y=%.2f z=%.2f | rms x=%.3f y=%.3f z=%.3f m/s2\n",
               (double)(s[0].max - s[0].min),
               (double)(s[1].max - s[1].min),
               (double)(s[2].max - s[2].min),
               (double)stats_rms(&s[0], n),
               (double)stats_rms(&s[1], n),
               (double)stats_rms(&s[2], n));
        printk("        | gravite moyenne x=%.2f y=%.2f z=%.2f m/s2\n",
               (double)(s[0].ref + s[0].somme / (float)n),
               (double)(s[1].ref + s[1].somme / (float)n),
               (double)(s[2].ref + s[2].somme / (float)n));
    }
    else
    {
        printk("        | vibrations --\n");
    }

    if (m.courant_ok)
    {
        printk("        | I=%.3f A U=%.1f V P=%.1f W PF=%.2f", (double)m.courant_a, (double)m.tension_v, (double)m.puissance_w, (double)m.facteur_p);
    }
    else
    {
        printk("        | I=--");
    }
    if (m.temperature_ok)
    {
        printk("  | T=%.1f C\n", (double)m.temperature_c);
    }
    else
    {
        printk("  | T=--\n");
    }
}

int main(void)
{
    float xyz[3];
    float tr_min = 0.0f;
    struct stats s[3];
    uint32_t n = 0;  /* ticks dans la seconde en cours */
    uint32_t nv = 0; /* echantillons de vibration valides */
    uint32_t sec = 0;

    printk("PredictMotor demarre\n");

    /* Le relais en premier : on s'assure que le ventilateur est arrete */
    if (moteur_init() < 0)
    {
        LOG_ERR("Relais non pret");
        return 0;
    }

    /* Capteurs optionnels : on continue meme s'ils manquent */
    bool vib_ok = (vibration_init() == 0);

    if (!vib_ok)
    {
        LOG_WRN("ADXL345 absent : vibrations non mesurees");
    }
    if (vitesse_init() < 0)
    {
        LOG_WRN("TCRT5000 non pret : vitesse non mesuree");
    }

    /* Capteurs lents : utiles mais pas vitaux, ils tournent a cote */
    capteurs_lents_demarrer();
    reseau_demarrer();

    if (IS_ENABLED(CONFIG_PM_MOTEUR_AU_DEMARRAGE))
    {
        moteur_marche();
    }
    else
    {
        LOG_INF("Ventilateur arrete. Taper \"moteur on\" pour le demarrer");
    }

    stats_reset(s);
    k_timer_start(&tick_timer, K_USEC(PERIODE_US), K_USEC(PERIODE_US));

    while (1)
    {
        k_sem_take(&tick_sem, K_FOREVER);

        if (vib_ok && vibration_lire(xyz) == 0)
        {
            stats_add(s, xyz);
            nv++;
        }

        if (++n == FENETRE)
        {
            sec++;
            (void)vitesse_lire(&tr_min);
            afficher_bilan(sec, tr_min, s, nv);
            stats_reset(s);
            n = 0;
            nv = 0;
        }
    }
    return 0;
}