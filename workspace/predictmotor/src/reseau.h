#ifndef RESEAU_H
#define RESEAU_H

#include <stdbool.h>

/* Lance le thread qui connecte l'ESP32 au Wi-Fi (et se reconnecte
 * tout seul en cas de coupure). Ne bloque pas. */
void reseau_demarrer(void);

/* true quand l'ESP32 est connecte au Wi-Fi ET a recu une adresse IP */
bool reseau_pret(void);

#endif