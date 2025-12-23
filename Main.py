from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles 
from pydantic import BaseModel
from typing import List
import sqlite3
from datetime import datetime
import math 
from collections import deque, Counter

DB_NAME = "wifi_scans.db"

app = FastAPI(title="WiFi Scan API")

app.add_middleware( #permet les requêtes cross-origin
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

class Measurement(BaseModel): # Mise en forme des données reçues
    ssid: str
    bssid: str
    rssi: int
    channel: int

# Modèle allégé pour le mode hors ligne (économie de mémoire)
class LiteMeasurement(BaseModel):
    bssid: str
    rssi: int

class ScanPayload(BaseModel): # Modèle pour l'apprentissage
    location: str  
    measurements: List[Measurement] # Liste des mesures WiFi

class LocatePayload(BaseModel): # Modèle pour la localisation en temps réel
    measurements: List[Measurement]

#Modèle pour recevoir l'historique complet d'un trajet
class HistoryStep(BaseModel):
    measurements: List[LiteMeasurement] 

class PathPayload(BaseModel): #Modèle pour la reconstruction de trajet
    history: List[HistoryStep] 


def get_connection(): # Connexion à la DB
    conn = sqlite3.connect(DB_NAME)
    conn.row_factory = sqlite3.Row
    return conn

def init_db(): # Initialisation de la DB
    conn = get_connection()
    cur = conn.cursor()
    cur.execute(
        """
        CREATE TABLE IF NOT EXISTS wifi_scan (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            location TEXT,
            ssid TEXT,
            bssid TEXT,
            rssi INTEGER,
            channel INTEGER,
            timestamp TEXT
        );
        """
    )
    conn.commit()
    conn.close()

init_db()


# Historique pour le lissage en temps réel
historique_localisation = deque(maxlen=5)

def localisation_proche(current_scan: List[LiteMeasurement]): # Trouver le lieu le plus proche
    conn = get_connection() # Connexion à la DB
    cur = conn.cursor() # Curseur pour exécuter les requêtes
    
    cur.execute(""" 
        SELECT location, bssid, AVG(rssi) as avg_rssi 
        FROM wifi_scan 
        GROUP BY location, bssid
    """)
    rows = cur.fetchall() # Récupération des données agrégées
    conn.close() # Fermeture de la connexion

    # Organisation des données
    data = {} 
    for row in rows:
        loc = row["location"]
        if loc not in data: data[loc] = {}
        # On stocke directement la moyenne calculée par SQL
        data[loc][row["bssid"]] = row["avg_rssi"]

    # Préparation du scan actuel
    scan_dict = {m.bssid: m.rssi for m in current_scan} #Dictionnaire stockant par BSSID les RSSI
    scan_bssids = set(scan_dict.keys()) #Ensemble des BSSID scannés

    best_location = "Inconnu" 
    min_error = float('inf') 
    
    # Seuil de sécurité : Il faut au moins 2 routeurs en commun pour valider
    MIN_MATCHES_REQUIRED = 2 

    for loc_name, db_fingerprint in data.items():
        error_score = 0
        
        # On compare uniquement les routeurs présents dans les deux listes
        db_bssids = set(db_fingerprint.keys())
        common_bssids = scan_bssids.intersection(db_bssids)
        
        matches = len(common_bssids)

        # Filtre pour éviter les fausse détections
        if matches < MIN_MATCHES_REQUIRED:
            continue 

        # Calcul de la Distance Euclidienne (Somme des carrés des différences)
        for bssid in common_bssids:
            diff = scan_dict[bssid] - db_fingerprint[bssid]
            error_score += diff * diff
            
        if matches > 0:
            # Normalisation par le nombre de routeurs communs, on normalise pour éviter de favoriser les lieux avec plus de routeurs
            final_score = math.sqrt(error_score) / matches
            
            if final_score < min_error:
                min_error = final_score
                best_location = loc_name
    
    return best_location

last_estimated_position = "En attente" #Pour stocker la dernière position estimée
last_calculated_path = [] # Pour stocker le trajet complet

# Enregistrement des données (Mode Learning)
@app.post("/scan")
def receive_scan(payload: ScanPayload):
    conn = get_connection()
    cur = conn.cursor()
    now = datetime.utcnow().isoformat() 

    for m in payload.measurements:
        cur.execute(
            "INSERT INTO wifi_scan (location, ssid, bssid, rssi, channel, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
            (payload.location, m.ssid, m.bssid, m.rssi, m.channel, now) #Insertion des données
        )
    conn.commit()
    conn.close()
    return {"status": "saved", "location": payload.location}

#Localisation Temps Réel
@app.post("/locate_me")
def locate_me(payload: LocatePayload):
    global last_estimated_position 
    
    lite_measurements = [LiteMeasurement(bssid=m.bssid, rssi=m.rssi) for m in payload.measurements] # vient récupérer dans la payload uniquement les champs nécessaires à la localisation et les transforme en LiteMeasurement
    instant_location = localisation_proche(lite_measurements)
    print(f"Live Scan: {instant_location}") #Affichage de la position instantanée

    if instant_location != "Inconnu": # On n'ajoute que les positions valides
        historique_localisation.append(instant_location) #Mise à jour de l'historique
    
    #Vote majoritaire sur les 5 derniers scans
    if len(historique_localisation) > 0:
        final_decision = Counter(historique_localisation).most_common(1)[0][0]
    else:
        final_decision = instant_location #Si pas d'historique, on prend la dernière détection

    last_estimated_position = final_decision #Mise à jour de la dernière position estimée
    return {"estimated_location": final_decision}

#Reconstruction de Trajet (Mode Offline)
@app.post("/reconstruct_path")
def reconstruct_path(payload: PathPayload):
    global last_calculated_path
    
    reconstructed_path = [] #Liste pour stocker le trajet reconstruit
    
    for step in payload.history:
        # On calcule la position pour chaque point de l'historique
        estimated_loc = localisation_proche(step.measurements)
        
        # On ne garde que les positions valides
        if estimated_loc != "Inconnu":
            if not reconstructed_path or reconstructed_path[-1] != estimated_loc: #Évite les doublons consécutifs
                reconstructed_path.append(estimated_loc) 
            
    print(f"Chemin reconstruit : {reconstructed_path}") #Affichage du trajet
    
    last_calculated_path = reconstructed_path #Mise à jour du trajet 
    return {"path": reconstructed_path}  #Retourne le trajet reconstruit

# Routes pour le HTML
@app.get("/current_position")
def get_current_position(): # Retourne la position actuelle estimée
    return {"location": last_estimated_position} #Retourne la dernière position estimée

@app.get("/current_path")
def get_path_for_map(): # Retourne le dernier trajet reconstruit
    return {"path": last_calculated_path} #Retourne le dernier trajet calculé

# Servir les fichiers statiques (images, html)
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/", include_in_schema=False)
def serve_index(): 
    return FileResponse("static/Site.html")
