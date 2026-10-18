#ifndef VIBRATION_H
#define VIBRATION_H

int vibration_init(void);
/* Lit l'acceleration en m/s2 sur x, y, z */
int vibration_lire(float xyz[3]);

#endif
