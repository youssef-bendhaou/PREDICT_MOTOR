/*
 * pzem.c : lecture du PZEM-004T v3.0 (Modbus RTU sur UART2, 9600 8N1)
 * PredictMotor, banc ventilateur 230 V
 *
 * Branchement (ESP32 38 broches) :
 *   PZEM RX  <- G17 (TX2)
 *   PZEM TX  -> G16 (RX2)
 *   PZEM 5V  -> 3V3, PZEM GND -> GND
 * Le PZEM ne répond que si L et N reçoivent le 230 V.
 */
#include "pzem.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pzem, LOG_LEVEL_INF);

#define PZEM_ADRESSE 0xF8    /* adresse générale (un seul PZEM) */
#define PZEM_FN_LECTURE 0x04 /* read input registers */
#define PZEM_NB_REGISTRES 10
#define PZEM_TAILLE_REPONSE (3 + 2 * PZEM_NB_REGISTRES + 2) /* 25 octets */
#define PZEM_TIMEOUT_MS 300

static const struct device *const uart_pzem = DEVICE_DT_GET(DT_NODELABEL(uart2));

static uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 1U) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

static void vider_reception(void)
{
    unsigned char c;

    while (uart_poll_in(uart_pzem, &c) == 0)
    {
    }
}

static int recevoir(uint8_t *buf, size_t len, int timeout_ms)
{
    size_t n = 0;
    int64_t limite = k_uptime_get() + timeout_ms;

    while (n < len)
    {
        unsigned char c;

        if (uart_poll_in(uart_pzem, &c) == 0)
        {
            buf[n++] = c;
        }
        else if (k_uptime_get() > limite)
        {
            return -ETIMEDOUT;
        }
        else
        {
            k_sleep(K_MSEC(1));
        }
    }
    return 0;
}

int pzem_init(void)
{
    if (!device_is_ready(uart_pzem))
    {
        LOG_ERR("UART2 non prêt (overlay ?)");
        return -ENODEV;
    }
    LOG_INF("PZEM sur UART2 (TX G17, RX G16), %d tour(s) dans le tore",
            CONFIG_PM_PZEM_TOURS);
    return 0;
}

int pzem_lire(struct pzem_mesure *m)
{
    uint8_t req[8] = {
        PZEM_ADRESSE,
        PZEM_FN_LECTURE,
        0x00,
        0x00, /* premier registre */
        0x00,
        PZEM_NB_REGISTRES, /* nombre de registres */
        0,
        0, /* CRC */
    };
    uint8_t rep[PZEM_TAILLE_REPONSE];
    uint16_t r[PZEM_NB_REGISTRES];
    uint16_t crc;
    int err;

    if (!device_is_ready(uart_pzem))
    {
        return -ENODEV;
    }

    crc = crc16_modbus(req, 6);
    req[6] = crc & 0xFF; /* CRC : octet faible d'abord */
    req[7] = crc >> 8;

    vider_reception();
    for (size_t i = 0; i < sizeof(req); i++)
    {
        uart_poll_out(uart_pzem, req[i]);
    }

    err = recevoir(rep, sizeof(rep), PZEM_TIMEOUT_MS);
    if (err)
    {
        return err;
    }

    crc = crc16_modbus(rep, sizeof(rep) - 2);
    if (rep[sizeof(rep) - 2] != (crc & 0xFF) || rep[sizeof(rep) - 1] != (crc >> 8))
    {
        return -EIO;
    }
    if (rep[0] != PZEM_ADRESSE || rep[1] != PZEM_FN_LECTURE ||
        rep[2] != 2 * PZEM_NB_REGISTRES)
    {
        return -EBADMSG;
    }

    for (int i = 0; i < PZEM_NB_REGISTRES; i++)
    {
        r[i] = ((uint16_t)rep[3 + 2 * i] << 8) | rep[4 + 2 * i];
    }

    /* Valeurs brutes (cf. doc PZEM-004T v3.0) */
    uint32_t courant_ma_brut = ((uint32_t)r[2] << 16) | r[1];   /* 0,001 A */
    uint32_t puissance_dw_brut = ((uint32_t)r[4] << 16) | r[3]; /* 0,1 W */
    uint32_t energie_wh_brut = ((uint32_t)r[6] << 16) | r[5];   /* 1 Wh */

    /* Correction des tours : seuls courant, puissance et énergie changent */
    m->tension_dv = r[0];
    m->courant_ua = courant_ma_brut * 1000U / CONFIG_PM_PZEM_TOURS;
    m->puissance_mw = puissance_dw_brut * 100U / CONFIG_PM_PZEM_TOURS;
    m->energie_wh = energie_wh_brut / CONFIG_PM_PZEM_TOURS;
    m->frequence_dhz = r[7];
    m->facteur_pc = r[8];
    m->alarme = (r[9] == 0xFFFF);

    return 0;
}