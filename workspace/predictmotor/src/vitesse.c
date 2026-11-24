#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include "vitesse.h"

#define UTILISATEUR DT_PATH(zephyr_user)

/* A 3150 tr/min un tour dure ~19 ms : tout front qui arrive moins de
 * 5 ms apres le precedent est un rebond du comparateur, on l'ignore */
#define ANTI_REBOND_US 5000

static const struct gpio_dt_spec tachy = GPIO_DT_SPEC_GET(UTILISATEUR, tachy_gpios);
static struct gpio_callback rappel;

static atomic_t impulsions;
static uint32_t dernier_front_cyc;
static int64_t t_prec;

static void front_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    uint32_t maintenant = k_cycle_get_32();

    if (k_cyc_to_us_floor32(maintenant - dernier_front_cyc) >= ANTI_REBOND_US)
    {
        atomic_inc(&impulsions);
        dernier_front_cyc = maintenant;
    }
}

int vitesse_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&tachy))
    {
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&tachy, GPIO_INPUT);
    if (ret < 0)
    {
        return ret;
    }
    ret = gpio_pin_interrupt_configure_dt(&tachy, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0)
    {
        return ret;
    }
    gpio_init_callback(&rappel, front_isr, BIT(tachy.pin));
    ret = gpio_add_callback(tachy.port, &rappel);

    atomic_set(&impulsions, 0);
    t_prec = k_uptime_get();
    return ret;
}

int vitesse_lire(float *tr_min)
{
    int64_t t = k_uptime_get();
    int64_t dt_ms = t - t_prec;

    if (dt_ms <= 0)
    {
        return -EAGAIN;
    }
    atomic_val_t n = atomic_set(&impulsions, 0);

    t_prec = t;
    *tr_min = (float)n * 60000.0f /
              ((float)dt_ms * (float)CONFIG_PM_IMPULSIONS_PAR_TOUR);
    return 0;
}