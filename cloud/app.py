"""
app.py : PredictMotor version cloud (un seul service web, pour Render).

  ESP32 / simulateur --HTTPS POST /api/mesures--> [ modele IA + vote 10 s ] --> page web /

Routes :
  POST /api/mesures?machine=banc1   recoit une mesure JSON, renvoie le diagnostic
  GET  /api/etat?machine=banc1      dernier diagnostic + historique (pour la page)
  GET  /                            tableau de bord
  GET  /sante                       verification de vie (Render)

Lancement local :  uvicorn app:app --reload
Variables d'environnement :
  CLE_API   cle secrete attendue dans l'en-tete X-Cle-API (vide = pas de controle)
  MODELE    chemin du .joblib (defaut : modele/predictmotor_modele.joblib)
  FENETRE_S taille du vote majoritaire en secondes (defaut 10)
"""
import os
import time
from collections import Counter, deque
from pathlib import Path

import joblib
import pandas as pd
from fastapi import FastAPI, Header, HTTPException
from fastapi.responses import HTMLResponse

ICI = Path(__file__).resolve().parent
CLE_API = os.getenv("CLE_API", "")
FENETRE = int(os.getenv("FENETRE_S", "10"))
HISTORIQUE = 900                      # 15 min a 1 mesure/s, en memoire
CODE_ETAT = {"arret": -1, "indisponible": -2, "normal": 0, "balourd": 1, "obstruction": 2}

paquet = joblib.load(os.getenv("MODELE", ICI / "modele" / "predictmotor_modele.joblib"))
CLF, DETECTEUR = paquet["classifieur"], paquet["detecteur"]
SEUIL, FEATURES = paquet["seuil"], paquet["features"]
CLASSES = list(CLF.classes_)


class Lisseur:
    """Vote majoritaire sur les N dernieres predictions (meme code que le notebook)."""

    def __init__(self, n=10):
        self.historique = deque(maxlen=n)

    def ajouter(self, etat):
        self.historique.append(etat)
        gagnant, nb = Counter(self.historique).most_common(1)[0]
        return gagnant, nb / len(self.historique)

    def vider(self):
        self.historique.clear()


class Machine:
    def __init__(self):
        self.lisseur = Lisseur(FENETRE)
        self.anomalies = deque(maxlen=FENETRE)
        self.historique = deque(maxlen=HISTORIQUE)


machines = {}
app = FastAPI(title="PredictMotor")


def diagnostiquer(m, mesure):
    if mesure.get("moteur", 1) == 0:
        m.lisseur.vider()
        m.anomalies.clear()
        return {"etat": "arret", "etat_code": CODE_ETAT["arret"]}

    manquantes = [f for f in FEATURES if mesure.get(f) is None]
    if manquantes:
        return {"etat": "indisponible", "etat_code": CODE_ETAT["indisponible"],
                "manquantes": manquantes}

    x = pd.DataFrame([[float(mesure[f]) for f in FEATURES]], columns=FEATURES)
    probas = CLF.predict_proba(x)[0]
    brut = CLASSES[probas.argmax()]
    etat, confiance = m.lisseur.ajouter(brut)

    score = float(DETECTEUR.decision_function(x)[0])
    m.anomalies.append(score < SEUIL)
    alerte = sum(m.anomalies) / len(m.anomalies) >= 0.5

    r = {"etat": etat, "etat_code": CODE_ETAT[etat], "confiance": round(confiance, 2),
         "etat_brut": brut, "score_anomalie": round(score, 4), "anomalie": int(alerte)}
    for classe, p in zip(CLASSES, probas):
        r[f"p_{classe}"] = round(float(p), 3)
    return r


@app.post("/api/mesures")
def recevoir(mesure: dict, machine: str = "banc1", x_cle_api: str = Header(default="")):
    if CLE_API and x_cle_api != CLE_API:
        raise HTTPException(status_code=401, detail="cle API invalide")
    m = machines.setdefault(machine, Machine())
    r = diagnostiquer(m, mesure)
    point = {"t": time.time(), **{k: mesure.get(k) for k in
             ("rms_x", "rms_y", "rms_z", "facteur_p", "puissance_w", "temp_c", "etat_simule")}, **r}
    m.historique.append(point)
    return r


@app.get("/api/etat")
def etat(machine: str = "banc1", n: int = 300):
    m = machines.get(machine)
    if not m or not m.historique:
        return {"machine": machine, "dernier": None, "historique": []}
    hist = list(m.historique)[-n:]
    return {"machine": machine, "dernier": hist[-1], "historique": hist,
            "age_s": round(time.time() - hist[-1]["t"], 1)}


@app.get("/sante")
def sante():
    return {"ok": True, "machines": list(machines), "features": FEATURES}


@app.get("/", response_class=HTMLResponse)
def page():
    return (ICI / "page.html").read_text(encoding="utf-8")
