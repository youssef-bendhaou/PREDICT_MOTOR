"""
app.py : PredictMotor version cloud (un seul service web, pour Render).

  ESP32 / simulateur --HTTPS POST /api/mesures--> [ modele IA + vote 10 s ] --> page web /
                                                            |
                                                            +--> alerte e-mail (Gmail) et/ou Telegram
                                                                 si un defaut est confirme

Routes :
  POST /api/mesures?machine=banc1   mesure JSON (en-tete X-Cle-API) -> diagnostic
  GET  /api/etat?machine=banc1      dernier diagnostic + historique (connexion requise)
  POST /api/test-alerte             envoie une alerte de test (connexion requise)
  GET  /login, POST /login, GET /logout
  GET  /                            tableau de bord (connexion requise)
  GET  /sante                       verification de vie (publique)

Variables d'environnement :
  CLE_API            cle attendue dans l'en-tete X-Cle-API (vide = pas de controle)
  ADMIN_USER         identifiant de connexion a la page (defaut : admin)
  ADMIN_PASSWORD     mot de passe de la page (vide = page sans connexion, pour les essais en local)
  SECRET_KEY         cle de signature des sessions (sinon generee au demarrage)
  EMAIL_RELAIS_URL   URL du relais Google Apps Script qui envoie l'e-mail depuis Gmail
  EMAIL_RELAIS_CLE   secret partage avec le relais (le relais refuse les autres)
  TELEGRAM_TOKEN     jeton du bot Telegram (optionnel)
  TELEGRAM_CHAT_ID   identifiant de la conversation Telegram (optionnel)
  DUREE_ALERTE_S     duree d'un defaut avant alerte (defaut 30 s)
  MODELE, FENETRE_S, SEUIL_PF, SEUIL_PPY
"""
import hashlib
import hmac
import json
import os
import secrets
import threading
import time
import urllib.parse
import urllib.request
from collections import Counter, deque
from pathlib import Path

import joblib
import pandas as pd
from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse

ICI = Path(__file__).resolve().parent
CLE_API = os.getenv("CLE_API", "")
ADMIN_USER = os.getenv("ADMIN_USER", "admin")
ADMIN_PASSWORD = os.getenv("ADMIN_PASSWORD", "")
SECRET_KEY = os.getenv("SECRET_KEY") or secrets.token_hex(32)
TELEGRAM_TOKEN = os.getenv("TELEGRAM_TOKEN", "")
TELEGRAM_CHAT_ID = os.getenv("TELEGRAM_CHAT_ID", "")
TELEGRAM_API = os.getenv("TELEGRAM_API", "https://api.telegram.org")
EMAIL_RELAIS_URL = os.getenv("EMAIL_RELAIS_URL", "")
EMAIL_RELAIS_CLE = os.getenv("EMAIL_RELAIS_CLE", "")
DUREE_ALERTE = int(os.getenv("DUREE_ALERTE_S", "30"))
FENETRE = int(os.getenv("FENETRE_S", "10"))
# Seuils de l'arbre de decision a 2 niveaux (notebook, cellule 12)
SEUIL_PF = float(os.getenv("SEUIL_PF", "0.715"))
SEUIL_PPY = float(os.getenv("SEUIL_PPY", "0.73"))

HISTORIQUE = 900                      # 15 min a 1 mesure/s, en memoire
DUREE_SESSION = 12 * 3600             # 12 h
CODE_ETAT = {"arret": -1, "indisponible": -2, "normal": 0, "balourd": 1, "obstruction": 2}
DEFAUTS = {"balourd", "obstruction"}

paquet = joblib.load(os.getenv("MODELE", ICI / "modele" / "predictmotor_modele.joblib"))
CLF, DETECTEUR = paquet["classifieur"], paquet["detecteur"]
SEUIL_ANOMALIE, FEATURES = paquet["seuil"], paquet["features"]
CLASSES = list(CLF.classes_)

app = FastAPI(title="PredictMotor")


# --------------------------------------------------------------------------- #
#  Authentification : session signee dans un cookie (HMAC-SHA256)
# --------------------------------------------------------------------------- #
def _signer(texte):
    return hmac.new(SECRET_KEY.encode(), texte.encode(), hashlib.sha256).hexdigest()


def creer_session(user):
    expire = int(time.time()) + DUREE_SESSION
    return f"{user}|{expire}|{_signer(f'{user}|{expire}')}"


def session_valide(jeton):
    try:
        user, expire, signature = (jeton or "").split("|")
        return (hmac.compare_digest(signature, _signer(f"{user}|{expire}"))
                and int(expire) > time.time())
    except ValueError:
        return False


def connecte(request: Request):
    return not ADMIN_PASSWORD or session_valide(request.cookies.get("session"))


# --------------------------------------------------------------------------- #
#  Alertes : e-mail via un relais Google Apps Script (Gmail) et/ou Telegram
#  (Render gratuit bloque le SMTP : on passe par HTTPS)
# --------------------------------------------------------------------------- #
def canaux_alerte():
    canaux = []
    if EMAIL_RELAIS_URL and EMAIL_RELAIS_CLE:
        canaux.append("email")
    if TELEGRAM_TOKEN and TELEGRAM_CHAT_ID:
        canaux.append("telegram")
    return canaux


def envoyer_alerte(sujet, texte):
    """Envoie l'alerte en arriere-plan (ne bloque jamais la reception des mesures)."""
    canaux = canaux_alerte()
    if not canaux:
        return []

    def tache():
        if "email" in canaux:
            try:
                corps = json.dumps({"cle": EMAIL_RELAIS_CLE, "sujet": sujet, "texte": texte}).encode()
                req = urllib.request.Request(EMAIL_RELAIS_URL, data=corps,
                                             headers={"Content-Type": "application/json"})
                rep = urllib.request.urlopen(req, timeout=20).read().decode(errors="ignore")
                if "ok" not in rep:
                    print(f"E-mail : le relais a refuse ({rep[:80]})")
            except Exception as e:
                print(f"E-mail : echec de l'envoi ({e})")
        if "telegram" in canaux:
            try:
                donnees = urllib.parse.urlencode({"chat_id": TELEGRAM_CHAT_ID,
                                                  "text": f"{sujet}\n{texte}"}).encode()
                urllib.request.urlopen(f"{TELEGRAM_API}/bot{TELEGRAM_TOKEN}/sendMessage",
                                       data=donnees, timeout=10).read()
            except Exception as e:
                print(f"Telegram : echec de l'envoi ({e})")

    threading.Thread(target=tache, daemon=True).start()
    return canaux


# --------------------------------------------------------------------------- #
#  Diagnostic
# --------------------------------------------------------------------------- #
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
        self.etat_courant = None      # etat lisse actuel
        self.depuis = time.time()     # debut de cet etat
        self.alerte_envoyee = None    # defaut deja signale (None = aucun)


machines = {}


def expliquer(mesure, etat):
    """Explication lisible, a partir des 2 regles de l'arbre de decision."""
    pf, ppy = mesure.get("facteur_p"), mesure.get("pp_y")
    if pf is None or ppy is None:
        return {"texte": "mesures incompletes", "regle": None}
    regle = "obstruction" if pf > SEUIL_PF else ("balourd" if ppy > SEUIL_PPY else "normal")
    f = lambda v, d=2: f"{v:.{d}f}".replace(".", ",")
    if regle == "obstruction":
        texte = (f"Facteur de puissance {f(pf)} > {f(SEUIL_PF, 3)} : "
                 "le moteur force sur l'air (charge anormale).")
    elif regle == "balourd":
        texte = (f"Vibration crête-à-crête y {f(ppy)} m/s² > {f(SEUIL_PPY)} "
                 f"(facteur de puissance normal : {f(pf)}) : rotor déséquilibré.")
    else:
        texte = (f"Facteur de puissance {f(pf)} ≤ {f(SEUIL_PF, 3)} et "
                 f"vibration crête-à-crête y {f(ppy)} ≤ {f(SEUIL_PPY)} m/s² : fonctionnement normal.")
    if etat in CODE_ETAT and etat not in ("arret", "indisponible") and regle != etat:
        texte += f" (Les règles simples indiquent « {regle} » : transition en cours ou cas limite.)"
    return {"texte": texte, "regle": regle}


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
    m.anomalies.append(score < SEUIL_ANOMALIE)
    alerte = sum(m.anomalies) / len(m.anomalies) >= 0.5

    r = {"etat": etat, "etat_code": CODE_ETAT[etat], "confiance": round(confiance, 2),
         "etat_brut": brut, "score_anomalie": round(score, 4), "anomalie": int(alerte)}
    for classe, p in zip(CLASSES, probas):
        r[f"p_{classe}"] = round(float(p), 3)
    r["explication"] = expliquer(mesure, etat)["texte"]
    return r


def surveiller(machine, m, r, mesure):
    """Alerte Telegram quand un defaut dure DUREE_ALERTE s, puis quand il disparait."""
    etat, maintenant = r["etat"], time.time()
    if etat != m.etat_courant:
        m.etat_courant, m.depuis = etat, maintenant
    duree = maintenant - m.depuis

    if etat in DEFAUTS and m.alerte_envoyee != etat and duree >= DUREE_ALERTE:
        m.alerte_envoyee = etat
        envoyer_alerte(
            f"⚠️ PredictMotor — {machine} : défaut « {etat} » détecté",
            f"Depuis {int(duree)} s, confiance du vote {int(r['confiance'] * 100)} %.\n"
            f"Pourquoi : {r['explication']}\n"
            f"Température moteur : {str(mesure.get('temp_c', '?')).replace('.', ',')} °C")
    elif etat == "normal" and m.alerte_envoyee and duree >= DUREE_ALERTE:
        envoyer_alerte(f"✅ PredictMotor — {machine} : retour à la normale",
                       f"Le défaut « {m.alerte_envoyee} » a disparu depuis {int(duree)} s.")
        m.alerte_envoyee = None


# --------------------------------------------------------------------------- #
#  Routes
# --------------------------------------------------------------------------- #
@app.post("/api/mesures")
def recevoir(mesure: dict, machine: str = "banc1", x_cle_api: str = Header(default="")):
    if CLE_API and not hmac.compare_digest(x_cle_api, CLE_API):
        raise HTTPException(status_code=401, detail="cle API invalide")
    m = machines.setdefault(machine, Machine())
    r = diagnostiquer(m, mesure)
    if "confiance" in r:
        surveiller(machine, m, r, mesure)
    point = {"t": time.time(), **{k: mesure.get(k) for k in
             ("rms_x", "rms_y", "rms_z", "pp_x", "pp_y", "pp_z", "facteur_p",
              "puissance_w", "temp_c", "etat_simule")}, **r}
    m.historique.append(point)
    return r


@app.get("/api/etat")
def etat(request: Request, machine: str = "banc1", n: int = 300):
    if not connecte(request):
        return JSONResponse({"detail": "connexion requise"}, status_code=401)
    m = machines.get(machine)
    if not m or not m.historique:
        return {"machine": machine, "dernier": None, "historique": [],
                "alertes": canaux_alerte()}
    hist = list(m.historique)[-n:]
    return {"machine": machine, "dernier": hist[-1], "historique": hist,
            "age_s": round(time.time() - hist[-1]["t"], 1),
            "depuis_s": round(time.time() - m.depuis),
            "alerte_en_cours": m.alerte_envoyee,
            "alertes": canaux_alerte()}


@app.post("/api/test-alerte")
def test_alerte(request: Request):
    if not connecte(request):
        return JSONResponse({"detail": "connexion requise"}, status_code=401)
    canaux = envoyer_alerte("🔔 PredictMotor : alerte de test",
                            "Si vous lisez ce message, les alertes fonctionnent.")
    return {"envoye": bool(canaux), "canaux": canaux,
            "detail": None if canaux else "aucun canal configure (EMAIL_RELAIS_* ou TELEGRAM_*)"}


@app.get("/login", response_class=HTMLResponse)
def page_login(erreur: int = 0):
    html = (ICI / "login.html").read_text(encoding="utf-8")
    return html.replace("{{ERREUR}}", "Identifiant ou mot de passe incorrect." if erreur else "")


@app.post("/login")
async def connexion(request: Request):
    champs = urllib.parse.parse_qs((await request.body()).decode())
    user = champs.get("user", [""])[0]
    mdp = champs.get("password", [""])[0]
    if ADMIN_PASSWORD and hmac.compare_digest(user, ADMIN_USER) \
            and hmac.compare_digest(mdp, ADMIN_PASSWORD):
        rep = RedirectResponse("/", status_code=303)
        https = request.headers.get("x-forwarded-proto", request.url.scheme) == "https"
        rep.set_cookie("session", creer_session(user), max_age=DUREE_SESSION,
                       httponly=True, secure=https, samesite="lax")
        return rep
    time.sleep(1)    # ralentit les essais de mot de passe en serie
    return RedirectResponse("/login?erreur=1", status_code=303)


@app.get("/logout")
def deconnexion():
    rep = RedirectResponse("/login", status_code=303)
    rep.delete_cookie("session")
    return rep


@app.get("/sante")
def sante():
    return {"ok": True, "machines": list(machines), "features": FEATURES,
            "connexion": bool(ADMIN_PASSWORD), "alertes": canaux_alerte()}


@app.get("/", response_class=HTMLResponse)
def page(request: Request):
    if not connecte(request):
        return RedirectResponse("/login", status_code=303)
    return (ICI / "page.html").read_text(encoding="utf-8")
