#ifndef MOTEUR_H
#define MOTEUR_H

int moteur_init(void);
/* Va progressivement vers une consigne de -100 (arriere) a +100 (avant) % */
int moteur_rampe(int cible);
/* Arret progressif puis coupure du driver */
void moteur_arret(void);

#endif
