/*
 * reseau.c : connexion Wi-Fi de l'ESP32 (station + DHCP)
 * PredictMotor
 *
 * Le nom et le mot de passe du Wi-Fi viennent de wifi.conf
 * (CONFIG_PM_WIFI_SSID / CONFIG_PM_WIFI_PSK), fichier non commite.
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include "reseau.h"

LOG_MODULE_REGISTER(reseau, LOG_LEVEL_INF);

#define PRIORITE 8 /* moins prioritaire que l'echantillonnage */
#define TAILLE_PILE 3072
#define ATTENTE_IP_S 30 /* delai max pour obtenir une IP */
#define PAUSE_ESSAI_S 5 /* pause entre deux tentatives */

#define EVTS_WIFI (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback cb_wifi;
static struct net_mgmt_event_callback cb_ip;
static K_SEM_DEFINE(sem_ip, 0, 1);
static atomic_t pret = ATOMIC_INIT(0);

K_THREAD_STACK_DEFINE(pile_reseau, TAILLE_PILE);
static struct k_thread fil_reseau;

static void evenement_wifi(struct net_mgmt_event_callback *cb,
                           uint32_t evt, struct net_if *iface)
{
    ARG_UNUSED(iface);

    if (evt == NET_EVENT_WIFI_CONNECT_RESULT)
    {
        const struct wifi_status *st = cb->info;

        if (st && st->status)
        {
            LOG_WRN("Wi-Fi : echec de connexion (%d), mauvais SSID ou mot de passe ?",
                    st->status);
        }
        else
        {
            LOG_INF("Wi-Fi connecte, attente de l'adresse IP...");
        }
    }
    else if (evt == NET_EVENT_WIFI_DISCONNECT_RESULT)
    {
        if (atomic_set(&pret, 0))
        {
            LOG_WRN("Wi-Fi perdu, reconnexion...");
        }
    }
}

static void evenement_ip(struct net_mgmt_event_callback *cb,
                         uint32_t evt, struct net_if *iface)
{
    ARG_UNUSED(iface);

    if (evt != NET_EVENT_IPV4_ADDR_ADD)
    {
        return;
    }

    char txt[NET_IPV4_ADDR_LEN] = "?";

    if (cb->info)
    {
        net_addr_ntop(AF_INET, cb->info, txt, sizeof(txt));
    }
    LOG_INF("Adresse IP : %s", txt);
    atomic_set(&pret, 1);
    k_sem_give(&sem_ip);
}

static int demander_connexion(struct net_if *iface)
{
    struct wifi_connect_req_params p = {0};

    p.ssid = (const uint8_t *)CONFIG_PM_WIFI_SSID;
    p.ssid_length = strlen(CONFIG_PM_WIFI_SSID);
    p.psk = (const uint8_t *)CONFIG_PM_WIFI_PSK;
    p.psk_length = strlen(CONFIG_PM_WIFI_PSK);
    p.security = p.psk_length ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
    p.band = WIFI_FREQ_BAND_2_4_GHZ; /* l'ESP32 ne fait que du 2,4 GHz */
    p.channel = WIFI_CHANNEL_ANY;
    p.mfp = WIFI_MFP_OPTIONAL;
    p.timeout = SYS_FOREVER_MS;

    LOG_INF("Connexion au Wi-Fi \"%s\"...", CONFIG_PM_WIFI_SSID);
    return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
}

static void boucle(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct net_if *iface = net_if_get_default();

    if (!iface)
    {
        LOG_ERR("Pas d'interface reseau (wifi active dans l'overlay ?)");
        return;
    }
    if (strlen(CONFIG_PM_WIFI_SSID) == 0)
    {
        LOG_ERR("SSID vide : compiler avec -DEXTRA_CONF_FILE=wifi.conf");
        return;
    }

    /* Laisse le pilote Wi-Fi finir de demarrer */
    k_sleep(K_SECONDS(2));

    while (1)
    {
        if (!atomic_get(&pret))
        {
            k_sem_reset(&sem_ip);
            int err = demander_connexion(iface);

            if (err)
            {
                LOG_WRN("Demande de connexion refusee (%d)", err);
            }
            else if (k_sem_take(&sem_ip, K_SECONDS(ATTENTE_IP_S)) != 0)
            {
                LOG_WRN("Pas d'adresse IP apres %d s", ATTENTE_IP_S);
            }

            if (!atomic_get(&pret))
            {
                k_sleep(K_SECONDS(PAUSE_ESSAI_S));
            }
        }
        else
        {
            k_sleep(K_SECONDS(1));
        }
    }
}

void reseau_demarrer(void)
{
    net_mgmt_init_event_callback(&cb_wifi, evenement_wifi, EVTS_WIFI);
    net_mgmt_add_event_callback(&cb_wifi);
    net_mgmt_init_event_callback(&cb_ip, evenement_ip, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&cb_ip);

    k_thread_create(&fil_reseau, pile_reseau, K_THREAD_STACK_SIZEOF(pile_reseau),
                    boucle, NULL, NULL, NULL, PRIORITE, 0, K_NO_WAIT);
    k_thread_name_set(&fil_reseau, "reseau");
}

bool reseau_pret(void)
{
    return atomic_get(&pret) != 0;
}