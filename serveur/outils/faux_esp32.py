"""
faux_esp32.py : simule le banc PredictMotor en rejouant de VRAIES mesures.

Il lit dataset_pzem.csv et envoie une ligne par seconde, au format JSON que
l'ESP32 enverra :
  - en MQTT (defaut) : sujet predictmotor/<machine>/mesures (pile Docker locale)
  - en HTTP (--url)  : POST <url>/api/mesures?machine=<machine> (version cloud / Render)

Scenario par defaut (en boucle) : 60 s normal, 60 s balourd,
60 s normal, 60 s obstruction.

Exemples :
    python faux_esp32.py
    python faux_esp32.py --csv ../../workspace/predictmotor/outils/dataset_pzem.csv
    python faux_esp32.py --duree-phase 30 --vitesse 5     (5 fois plus vite)
    python faux_esp32.py --url http://localhost:8000      (version cloud en local)
    python faux_esp32.py --url https://xxx.onrender.com --cle MA_CLE
"""
import argparse
import csv
import itertools
import json
import time
import urllib.error
import urllib.request
from collections import defaultdict
from pathlib import Path

# Champs envoyes : memes noms que les colonnes du dataset (= ce qu'enverra l'ESP32)
CHAMPS = ["rms_x", "rms_y", "rms_z", "pp_x", "pp_y", "pp_z",
          "g_x", "g_y", "g_z",
          "courant_a", "tension_v", "puissance_w", "facteur_p", "temp_c"]

# Pour verifier le diagnostic dans Grafana : uniquement envoye par le simulateur
CODE_ETAT = {"normal": 0, "balourd": 1, "obstruction": 2}
SCENARIO = ["normal", "balourd", "normal", "obstruction"]

CSV_DEFAUT = (Path(__file__).resolve().parents[2]
              / "workspace" / "predictmotor" / "outils" / "dataset_pzem.csv")


def charger(chemin):
    """Retourne {etat: [lignes]} en gardant l'ordre chronologique."""
    par_etat = defaultdict(list)
    with open(chemin, newline="", encoding="utf-8") as f:
        for ligne in csv.DictReader(f):
            if ligne["etat"] == "obstruction" and int(ligne["t_s"]) <= 43:
                continue    # plaque pas encore posee (cf. nettoyage du notebook)
            par_etat[ligne["etat"]].append(ligne)
    return par_etat


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--serveur", default="localhost")
    p.add_argument("--machine", default="banc1")
    p.add_argument("--csv", default=str(CSV_DEFAUT))
    p.add_argument("--duree-phase", type=int, default=60, help="secondes par phase")
    p.add_argument("--vitesse", type=float, default=1.0, help="x fois plus vite que le temps reel")
    p.add_argument("--url", help="envoi en HTTP vers ce serveur (ex. https://xxx.onrender.com)")
    p.add_argument("--cle", default="", help="cle API (en-tete X-Cle-API) pour --url")
    a = p.parse_args()

    par_etat = charger(a.csv)
    for etat in SCENARIO:
        if not par_etat.get(etat):
            raise SystemExit(f"Aucune ligne '{etat}' dans {a.csv}")
    curseurs = {e: itertools.cycle(lignes) for e, lignes in par_etat.items()}

    if a.url:
        adresse = f"{a.url.rstrip('/')}/api/mesures?machine={a.machine}"
        client = None

        def envoyer(mesure):
            req = urllib.request.Request(adresse, data=json.dumps(mesure).encode(),
                                         headers={"Content-Type": "application/json",
                                                  "X-Cle-API": a.cle}, method="POST")
            try:
                with urllib.request.urlopen(req, timeout=90) as rep:   # Render peut mettre ~1 min a se reveiller
                    return json.loads(rep.read()).get("etat", "?")
            except urllib.error.HTTPError as e:
                return f"erreur HTTP {e.code}"
            except Exception as e:
                return f"erreur : {e}"
        print(f"Envoi HTTP vers {adresse} (Ctrl+C pour arreter)")
    else:
        import paho.mqtt.client as mqtt
        sujet = f"predictmotor/{a.machine}/mesures"
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        client.connect(a.serveur, 1883)
        client.loop_start()

        def envoyer(mesure):
            client.publish(sujet, json.dumps(mesure))
            return ""
        print(f"Envoi MQTT sur {sujet} (Ctrl+C pour arreter)")

    t = 0
    try:
        for etat in itertools.cycle(SCENARIO):
            print(f"\n--- phase {etat} ({a.duree_phase} s) ---")
            for _ in range(a.duree_phase):
                ligne = next(curseurs[etat])
                mesure = {c: float(ligne[c]) for c in CHAMPS if ligne.get(c) not in (None, "")}
                mesure["moteur"] = 1
                mesure["etat_simule"] = CODE_ETAT[etat]
                reponse = envoyer(mesure)
                print(f"t={t:4d} s  {etat:<11}  rms_y={mesure['rms_y']:.3f}  "
                      f"PF={mesure['facteur_p']:.2f}  P={mesure['puissance_w']:.1f} W"
                      + (f"  -> diagnostic : {reponse}" if reponse else ""))
                t += 1
                time.sleep(1 / a.vitesse)
    except KeyboardInterrupt:
        pass
    finally:
        if client:
            client.loop_stop()
            client.disconnect()
        print("\nArrete.")


if __name__ == "__main__":
    main()
