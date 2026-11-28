"""
diagnostic.py : service de diagnostic en temps reel de PredictMotor.

  predictmotor/<machine>/mesures  --[modele IA + vote sur N s]-->  predictmotor/<machine>/diagnostic

Pour chaque mesure recue (1 par seconde), il :
  1. applique le classifieur (normal / balourd / obstruction) ;
  2. lisse la decision par un vote majoritaire sur les N dernieres secondes ;
  3. calcule le score du detecteur d'anomalies (entraine sur le normal seul) ;
  4. publie le resultat en JSON (Telegraf le range dans InfluxDB pour Grafana).

Exemples :
    python diagnostic.py
    python diagnostic.py --serveur localhost --modele ../modele/predictmotor_modele.joblib
"""
import argparse
import json
import os
from collections import Counter, deque
from pathlib import Path

import joblib
import pandas as pd
import paho.mqtt.client as mqtt

CODE_ETAT = {"arret": -1, "indisponible": -2, "normal": 0, "balourd": 1, "obstruction": 2}
MODELE_DEFAUT = Path(__file__).resolve().parent.parent / "modele" / "predictmotor_modele.joblib"


class Lisseur:
    """Vote majoritaire sur les N dernieres predictions (meme code que dans le notebook)."""

    def __init__(self, n=10):
        self.historique = deque(maxlen=n)

    def ajouter(self, etat):
        self.historique.append(etat)
        gagnant, nb = Counter(self.historique).most_common(1)[0]
        return gagnant, nb / len(self.historique)

    def vider(self):
        self.historique.clear()


class Diagnostic:
    def __init__(self, chemin_modele, fenetre):
        paquet = joblib.load(chemin_modele)
        self.clf = paquet["classifieur"]
        self.detecteur = paquet["detecteur"]
        self.seuil = paquet["seuil"]
        self.features = paquet["features"]
        self.classes = list(self.clf.classes_)
        self.fenetre = fenetre
        self.lisseurs = {}      # un lisseur par machine
        self.anomalies = {}     # historique des anomalies par machine
        print(f"Modele charge : {len(self.features)} variables {self.features}")
        print(f"Classes : {self.classes} | seuil d'anomalie : {self.seuil:.3f} "
              f"| vote sur {fenetre} s | scikit-learn du modele : {paquet.get('sklearn_version')}")

    def analyser(self, machine, mesure):
        lisseur = self.lisseurs.setdefault(machine, Lisseur(self.fenetre))
        hist_anom = self.anomalies.setdefault(machine, deque(maxlen=self.fenetre))

        # Moteur arrete : le modele n'a appris que le moteur en marche
        if mesure.get("moteur", 1) == 0:
            lisseur.vider()
            hist_anom.clear()
            return {"etat": "arret", "etat_code": CODE_ETAT["arret"]}

        manquantes = [f for f in self.features if mesure.get(f) is None]
        if manquantes:      # ex. PZEM muet -> pas de facteur_p
            return {"etat": "indisponible", "etat_code": CODE_ETAT["indisponible"],
                    "manquantes": ",".join(manquantes)}

        x = pd.DataFrame([[float(mesure[f]) for f in self.features]], columns=self.features)

        probas = self.clf.predict_proba(x)[0]
        brut = self.classes[probas.argmax()]
        etat, confiance = lisseur.ajouter(brut)

        score = float(self.detecteur.decision_function(x)[0])
        hist_anom.append(score < self.seuil)
        alerte = sum(hist_anom) / len(hist_anom) >= 0.5     # anomalie sur la moitie de la fenetre

        resultat = {
            "etat": etat,
            "etat_code": CODE_ETAT[etat],
            "confiance": round(confiance, 2),
            "etat_brut_code": CODE_ETAT[brut],
            "score_anomalie": round(score, 4),
            "anomalie": int(alerte),
        }
        for classe, p in zip(self.classes, probas):
            resultat[f"p_{classe}"] = round(float(p), 3)
        return resultat


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--serveur", default=os.getenv("MQTT_SERVEUR", "localhost"))
    p.add_argument("--port", type=int, default=int(os.getenv("MQTT_PORT", "1883")))
    p.add_argument("--modele", default=os.getenv("MODELE", str(MODELE_DEFAUT)))
    p.add_argument("--fenetre", type=int, default=int(os.getenv("FENETRE_S", "10")),
                   help="secondes du vote majoritaire")
    a = p.parse_args()

    diag = Diagnostic(a.modele, a.fenetre)

    def connexion(client, userdata, flags, code, proprietes):
        if code == 0:
            client.subscribe("predictmotor/+/mesures")
            print(f"Connecte a {a.serveur}:{a.port}, abonne a predictmotor/+/mesures")
        else:
            print(f"Connexion MQTT refusee : {code}")

    def message(client, userdata, msg):
        try:
            machine = msg.topic.split("/")[1]
            mesure = json.loads(msg.payload)
            r = diag.analyser(machine, mesure)
        except Exception as e:      # un message abime ne doit pas arreter le service
            print(f"Message ignore ({msg.topic}) : {e}")
            return

        client.publish(f"predictmotor/{machine}/diagnostic", json.dumps(r))

        if "confiance" in r:
            print(f"{machine} | {r['etat']:<11} (votes {r['confiance']:.0%}) "
                  f"| brut={r['etat_brut_code']:>2} | anomalie={r['anomalie']} "
                  f"(score {r['score_anomalie']:+.3f})")
        else:
            print(f"{machine} | {r['etat']}")

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="predictmotor-diagnostic")
    client.on_connect = connexion
    client.on_message = message
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    client.connect(a.serveur, a.port)
    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nArrete.")


if __name__ == "__main__":
    main()
