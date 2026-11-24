import re, csv, os, time, argparse
import serial

ANSI = re.compile(r'\x1b\[[0-9;?]*[A-Za-z]')
NUM = r'(-?\d+(?:\.\d+)?)'
COLS = ['horodatage', 'etat', 't_s', 'moteur', 'tr_min',
        'pp_x', 'pp_y', 'pp_z', 'rms_x', 'rms_y', 'rms_z',
        'g_x', 'g_y', 'g_z', 'temp_c']

RE_T   = re.compile(r't=\s*(\d+)\s*s\s*\|\s*moteur\s+(ON|OFF)\s*\|\s*(\d+)\s*tr/min')
RE_VIB = re.compile(r'crete-a-crete\s+x=' + NUM + r'\s+y=' + NUM + r'\s+z=' + NUM +
                    r'\s*\|\s*rms\s+x=' + NUM + r'\s+y=' + NUM + r'\s+z=' + NUM)
RE_G   = re.compile(r'gravite moyenne\s+x=' + NUM + r'\s+y=' + NUM + r'\s+z=' + NUM)
RE_TC  = re.compile(r'T=\s*(-?\d+(?:\.\d+)?|--)')

p = argparse.ArgumentParser()
p.add_argument('etat', help='ex: normal, obstruction, test')
p.add_argument('--port', default='COM4')
p.add_argument('--duree', type=int, default=1800, help='secondes')
p.add_argument('--ignorer', type=int, default=30, help='demarrage ignore (s)')
p.add_argument('--fichier', default='dataset.csv')
p.add_argument('--debug', action='store_true', help='affiche les lignes brutes')
a = p.parse_args()

# Ouverture du port comme miniterm (l'ESP32 peut redemarrer : on attend le boot)
ser = serial.Serial(a.port, 115200, timeout=0.2)
print(f'Port {a.port} ouvert, attente du demarrage de l ESP32...')
time.sleep(3)
ser.reset_input_buffer()
ser.write(b'\r')
time.sleep(0.3)
ser.write(b'moteur on\r')
print('Commande "moteur on" envoyee')

nouveau = not os.path.exists(a.fichier) or os.path.getsize(a.fichier) == 0
f = open(a.fichier, 'a', newline='')
w = csv.writer(f)
if nouveau:
    w.writerow(COLS)

rec, n = {}, 0
tampon = ''
debut = time.time()
dernier_rx = time.time()
print(f'Enregistrement "{a.etat}" pendant {a.duree} s (Ctrl+C pour arreter)')


def traiter(ligne):
    global rec, n
    m = RE_T.search(ligne)
    if m:
        rec = {'t_s': m[1], 'moteur': m[2], 'tr_min': m[3]}
    m = RE_VIB.search(ligne)
    if m and rec:
        rec.update(zip(['pp_x', 'pp_y', 'pp_z', 'rms_x', 'rms_y', 'rms_z'], m.groups()))
    m = RE_G.search(ligne)
    if m and rec:
        rec.update(zip(['g_x', 'g_y', 'g_z'], m.groups()))
    m = RE_TC.search(ligne)
    if m and 'g_x' in rec:
        rec['temp_c'] = '' if m[1] == '--' else m[1]
        if time.time() - debut >= a.ignorer:
            rec['horodatage'] = time.strftime('%Y-%m-%d %H:%M:%S')
            rec['etat'] = a.etat
            w.writerow([rec.get(c, '') for c in COLS])
            f.flush()
            n += 1
            if n % 10 == 0:
                print(f'{n} lignes | {rec["moteur"]} {rec["tr_min"]} tr/min | '
                      f'rms z={rec["rms_z"]} | T={rec["temp_c"]}')
        rec = {}


try:
    while time.time() - debut < a.duree:
        data = ser.read(512)
        if data:
            dernier_rx = time.time()
            tampon += data.decode('utf-8', 'ignore')
            while '\n' in tampon:
                brut, tampon = tampon.split('\n', 1)
                ligne = ANSI.sub('', brut).replace('\x1b', '')
                ligne = ligne.replace('uart:~$', '').replace('\r', '')
                if a.debug:
                    print(repr(ligne[:120]))
                if ligne.strip():
                    traiter(ligne)
        elif time.time() - dernier_rx > 5:
            print('Aucune donnee recue depuis 5 s : verifier le port et l ESP32')
            dernier_rx = time.time()
except KeyboardInterrupt:
    pass
finally:
    ser.write(b'moteur off\r')
    time.sleep(0.3)
    f.close()
    ser.close()
    print(f'Termine : {n} lignes ajoutees dans {a.fichier}')