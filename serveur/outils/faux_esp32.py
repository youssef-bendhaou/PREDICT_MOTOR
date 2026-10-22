"""Simule le banc PredictMotor : envoie une mesure par seconde en MQTT."""
import json
import math
import random
import time

import paho.mqtt.client as mqtt

SERVEUR = "localhost"
SUJET = "predictmotor/banc1/mesures"

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.connect(SERVEUR, 1883)
client.loop_start()

t = 0
print("Envoi des mesures... Ctrl+C pour arreter")
while True:
    balourd = (t // 30) % 2 == 1          # 30 s sain, 30 s balourd, etc.
    base = 1.6 if balourd else 0.3
    mesure = {
        "tr_min": 3000 + random.uniform(-20, 20),
        "cc_x": base + random.uniform(-0.05, 0.05),
        "cc_y": base * 0.6 + random.uniform(-0.05, 0.05),
        "cc_z": 0.1 + random.uniform(-0.02, 0.02),
        "courant_a": (1.4 if balourd else 1.2) + random.uniform(-0.03, 0.03),
        "temperature_c": 30 + 5 * math.sin(t / 200),
        "defaut": 1 if balourd else 0,
    }
    client.publish(SUJET, json.dumps(mesure))
    print(SUJET, mesure)
    t += 1
    time.sleep(1)
