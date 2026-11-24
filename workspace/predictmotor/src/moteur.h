#ifndef MOTEUR_H
#define MOTEUR_H

#include <stdbool.h>

/* Configure le relais et laisse le ventilateur ARRETE */
int moteur_init(void);
/* Ferme le relais : le ventilateur 230 V demarre */
int moteur_marche(void);
/* Ouvre le relais : le ventilateur s'arrete */
int moteur_arret(void);
/* true si le relais est ferme */
bool moteur_en_marche(void);

#endif