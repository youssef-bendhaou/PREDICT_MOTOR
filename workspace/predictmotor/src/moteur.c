#include <errno.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include "moteur.h"

#define UTILISATEUR DT_PATH(zephyr_user)

#define PAS_RAMPE    5    /* % par pas */
#define DELAI_RAMPE  50   /* ms entre deux pas */

static const struct pwm_dt_spec pwm_avant   = PWM_DT_SPEC_GET_BY_IDX(UTILISATEUR, 0);
static const struct pwm_dt_spec pwm_arriere = PWM_DT_SPEC_GET_BY_IDX(UTILISATEUR, 1);
static const struct gpio_dt_spec enable     = GPIO_DT_SPEC_GET(UTILISATEUR, moteur_en_gpios);

static int consigne_actuelle;

/* BTS7960 : une seule entree PWM active a la fois, l'autre a 0 */
static int appliquer(int pourcent)
{
    uint32_t impulsion = pwm_avant.period / 100U * (uint32_t)abs(pourcent);
    int ret;

    if (pourcent >= 0) {
        ret = pwm_set_pulse_dt(&pwm_arriere, 0);
        if (ret == 0) {
            ret = pwm_set_pulse_dt(&pwm_avant, impulsion);
        }
    } else {
        ret = pwm_set_pulse_dt(&pwm_avant, 0);
        if (ret == 0) {
            ret = pwm_set_pulse_dt(&pwm_arriere, impulsion);
        }
    }
    return ret;
}

int moteur_init(void)
{
    if (!pwm_is_ready_dt(&pwm_avant) || !pwm_is_ready_dt(&pwm_arriere) ||
        !gpio_is_ready_dt(&enable)) {
        return -ENODEV;
    }

    /* Securite : driver coupe et PWM a zero avant tout */
    int ret = gpio_pin_configure_dt(&enable, GPIO_OUTPUT_INACTIVE);

    if (ret < 0) {
        return ret;
    }
    consigne_actuelle = 0;
    ret = appliquer(0);
    if (ret < 0) {
        return ret;
    }
    return gpio_pin_set_dt(&enable, 1);
}

int moteur_rampe(int cible)
{
    cible = CLAMP(cible, -100, 100);

    /* Monter ou descendre par petits pas : evite les pics de courant */
    while (consigne_actuelle != cible) {
        if (consigne_actuelle < cible) {
            consigne_actuelle = MIN(consigne_actuelle + PAS_RAMPE, cible);
        } else {
            consigne_actuelle = MAX(consigne_actuelle - PAS_RAMPE, cible);
        }
        int ret = appliquer(consigne_actuelle);

        if (ret < 0) {
            return ret;
        }
        k_msleep(DELAI_RAMPE);
    }
    return 0;
}

void moteur_arret(void)
{
    (void)moteur_rampe(0);
    (void)gpio_pin_set_dt(&enable, 0);
}
