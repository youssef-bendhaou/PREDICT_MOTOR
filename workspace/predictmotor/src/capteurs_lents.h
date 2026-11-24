#ifndef CAPTEURS_LENTS_H
#define CAPTEURS_LENTS_H

#include <stdbool.h>

/* Les dernieres mesures des capteurs lents */
struct mesures_lentes
{
    float courant_a;   /* courant efficace du ventilateur (TA12-100) */
    float decalage_mv; /* milieu du signal : ~1650 mV si le pont diviseur est bon */
    float temperature_c;
    bool courant_ok;
    bool temperature_ok;
};

/* Lance le thread qui lit courant et temperature en arriere-plan */
void capteurs_lents_demarrer(void);

/* Copie les dernieres valeurs (sans jamais bloquer longtemps) */
void capteurs_lents_lire(struct mesures_lentes *m);

#endif