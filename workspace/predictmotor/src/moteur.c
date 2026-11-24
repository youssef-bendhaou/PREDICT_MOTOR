#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include "moteur.h"

LOG_MODULE_REGISTER(moteur, LOG_LEVEL_INF);

#define UTILISATEUR DT_PATH(zephyr_user)

/* Ventilateur Sunon DP200A (moteur a poles ombres) : pas de variation
 * de vitesse, seulement marche / arret par le relais */
static const struct gpio_dt_spec relais = GPIO_DT_SPEC_GET(UTILISATEUR, relais_gpios);

static bool en_marche;

int moteur_init(void)
{
    if (!gpio_is_ready_dt(&relais))
    {
        return -ENODEV;
    }
    /* Securite : relais ouvert (ventilateur arrete) avant tout */
    en_marche = false;
    return gpio_pin_configure_dt(&relais, GPIO_OUTPUT_INACTIVE);
}

int moteur_marche(void)
{
    int ret = gpio_pin_set_dt(&relais, 1);

    if (ret == 0)
    {
        en_marche = true;
        LOG_INF("ventilateur EN MARCHE");
    }
    return ret;
}

int moteur_arret(void)
{
    int ret = gpio_pin_set_dt(&relais, 0);

    if (ret == 0)
    {
        en_marche = false;
        LOG_INF("ventilateur ARRETE");
    }
    return ret;
}

bool moteur_en_marche(void)
{
    return en_marche;
}

/* Commande console : moteur on | moteur off */
static int cmd_moteur(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);

    if (strcmp(argv[1], "on") == 0)
    {
        return moteur_marche();
    }
    if (strcmp(argv[1], "off") == 0)
    {
        return moteur_arret();
    }
    shell_error(sh, "usage : moteur on|off");
    return -EINVAL;
}
SHELL_CMD_ARG_REGISTER(moteur, NULL, "Marche/arret du ventilateur : moteur on|off",
                       cmd_moteur, 2, 0);