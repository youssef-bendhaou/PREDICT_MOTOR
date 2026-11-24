#ifndef CAPTEURS_LENTS_H
#define CAPTEURS_LENTS_H

#include <stdbool.h>
#include <stdint.h>

/* Les dernieres mesures des capteurs lents (PZEM-004T + DS18B20) */
struct mesures_lentes
{
    /* PZEM-004T : courant, puissance et energie deja divises
     * par CONFIG_PM_PZEM_TOURS (passages de la phase dans le tore) */
    float courant_a;     /* courant efficace du ventilateur */
    float tension_v;     /* tension secteur */
    float puissance_w;   /* puissance active */
    float facteur_p;     /* facteur de puissance (0 a 1) */
    float frequence_hz;  /* frequence secteur */
    uint32_t energie_wh; /* energie cumulee */
    bool courant_ok;     /* true si le PZEM a repondu */

    /* DS18B20 */
    float temperature_c;
    bool temperature_ok;
};

/* Lance le thread qui lit le PZEM et la temperature en arriere-plan */
void capteurs_lents_demarrer(void);

/* Copie les dernieres valeurs (sans jamais bloquer longtemps) */
void capteurs_lents_lire(struct mesures_lentes *m);

#endif