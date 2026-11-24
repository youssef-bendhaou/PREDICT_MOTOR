#ifndef VITESSE_H
#define VITESSE_H

int vitesse_init(void);
/* Vitesse de l'helice depuis le dernier appel, en tr/min (TCRT5000) */
int vitesse_lire(float *tr_min);

#endif