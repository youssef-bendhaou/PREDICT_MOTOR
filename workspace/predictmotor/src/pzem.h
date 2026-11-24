/*
 * pzem.h : lecture du PZEM-004T v3.0 (Modbus RTU sur UART2, 9600 8N1)
 * PredictMotor, banc ventilateur 230 V
 */
#ifndef PZEM_H
#define PZEM_H

#include <stdbool.h>
#include <stdint.h>

/* Valeurs déjà corrigées du nombre de tours dans le tore
 * (CONFIG_PM_PZEM_TOURS) pour le courant, la puissance et l'énergie.
 * Unités entières pour éviter printf en flottant. */
struct pzem_mesure
{
    uint32_t tension_dv;    /* tension en 0,1 V        (2301 = 230,1 V) */
    uint32_t courant_ua;    /* courant en µA           (140000 = 0,140 A) */
    uint32_t puissance_mw;  /* puissance active en mW  (21000 = 21,0 W) */
    uint32_t energie_wh;    /* énergie cumulée en Wh */
    uint32_t frequence_dhz; /* fréquence en 0,1 Hz     (500 = 50,0 Hz) */
    uint32_t facteur_pc;    /* facteur de puissance en centièmes (92 = 0,92) */
    bool alarme;            /* alarme de surpuissance du PZEM */
};

/* Vérifie que l'UART2 est prêt. 0 si OK, -ENODEV sinon. */
int pzem_init(void);

/* Envoie une requête et lit les 10 registres de mesure.
 * Retour : 0 si OK
 *          -ENODEV    UART non prêt
 *          -ETIMEDOUT pas de réponse (PZEM sans 230 V, RX/TX inversés...)
 *          -EIO       CRC faux
 *          -EBADMSG   trame inattendue */
int pzem_lire(struct pzem_mesure *m);

#endif /* PZEM_H */