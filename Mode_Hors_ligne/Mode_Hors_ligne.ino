#include <WiFi.h>
#include <HTTPClient.h>

const char* WIFI_SSID     = "Benoit's Galaxy S21 FE 5G";
const char* WIFI_PASSWORD = "benoit12";
const char* SERVER_URL_LIVE = "http://10.223.162.162:8000/locate_me"; 
const char* SERVER_URL_PATH = "http://10.223.162.162:8000/reconstruct_path";

const int filtre_RSSI = -85;

const int NB_MAX_POINTS = 50; //nombre max de scans mémorisés
const int NB_MAX_RESEAUX_PAR_POINT = 20; //nombre max de réseaux par scan

//structure d'une mesure wifi pour la partie hors ligne
struct MesureWifi {
    String bssid;
    int rssi;
};

//structure d'un scan complet. Il regroupe un nombre de mesures et un tableau de mesures se limitant à 20 réseaux par scan pour éviter de saturer l'ESP
struct PointTemps {
    int nbMesures; //nombre de réseaux réellement trouvés
    MesureWifi mesures[NB_MAX_RESEAUX_PAR_POINT]; //tableau fixe de mesures
};

//Historique des points hors-ligne
PointTemps historiquePoints[NB_MAX_POINTS];
int nbPointsDansHistorique = 0;

//Variable pour savoir si on était connecté le tour d'avant
bool wasConnected = false;

void connectToWiFi() {
    Serial.println("\nDémarrage connexion WiFi");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int essaie = 0;
    while (WiFi.status() != WL_CONNECTED && essaie < 5) {
        delay(500);
        Serial.print(".");
        essaie++;
    }

    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("\nMode en ligne");
    } else {
        Serial.println("\nMode hors ligne");
    }
}

bool Filtre_BSSID(uint8_t* bssid) { //fonction permettant de filtrer les adresses mac volatiles
    return (bssid[0] & 0x02); //Pour savoir si c'est volatile ou non il faut regarder le bit 2 du quartet le plus haut
}

void sendLiveScan(String json) { // Lorsque nous sommes connecté en mode en ligne on envoie les données de façon constante à l'adresse SERVER_URL_LIVE
    HTTPClient http; //on redéfinie la variable http sous la structure HTTPClient afin d'utiliser les fonctions de la librairie HTTP
    http.begin(SERVER_URL_LIVE); //connexion au serveur à la route définie par SERVER_URL_LIVE
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(json);
    
    if (httpCode > 0 && httpCode >= 200 && httpCode < 300) { //Vérification qu'il y a un retour du serveur
        Serial.println("data envoyé");
    } else {
        Serial.print("Erreur envoi");
        Serial.println(http.errorToString(httpCode));
    }
    http.end(); //Fermeture de la connexion 
}

void envoyerHistoriqueChemin() {
    if (nbPointsDansHistorique == 0) return; //vérification de la présence de point dans l'historique

    Serial.print("reconnexion :");
    Serial.print(nbPointsDansHistorique); //envoie le nombre de point détecté lors de notre parcours
    Serial.println(" points)...");

    String json = "{ \"history\": ["; //formatage en JSON de notre payload afin que notre serveur puisse le décoder

    for (int i = 0; i < nbPointsDansHistorique; i++) {
        json += "{ \"measurements\": [";

        for (int j = 0; j < historiquePoints[i].nbMesures; j++) { //double boucle sur le nombre de scan (i) puis sur le nombre de réseau par scan (j).
            json += "{";
            json += "\"bssid\":\"" + historiquePoints[i].mesures[j].bssid + "\","; //parcours chaque scan en cherchant chaque BSSID de chaque réseau.
            json += "\"rssi\":" + String(historiquePoints[i].mesures[j].rssi);//parcours chaque scan en cherchant chaque RSSI de chaque réseau.
            json += "}";

            if (j < historiquePoints[i].nbMesures - 1) json += ","; //met une virgule entre chaque mesure de réseau sauf au dernier réseau pour le formatage du JSON
        }

        json += "] }";

        if (i < nbPointsDansHistorique - 1) json += ","; //met une virgule entre chaque scan sauf le dernier pour le formatage du JSON
    }

    json += "] }"; //Fermeture du JSON, donc dernier élément de la payload

    HTTPClient http;
    http.begin(SERVER_URL_PATH); //connexion à la route SERVER_URL_PATH de notre serveur FASTAPI
    http.addHeader("Content-Type", "application/json");
    int codeHTTP = http.POST(json);

    if (codeHTTP > 0 && codeHTTP >= 200 && codeHTTP < 300) { //Vérification de la présence d'un retour de notre serveur
        Serial.println("Historique envoye.");
        nbPointsDansHistorique = 0;  //on vide l'historique en remettant à 0
    } else {
        Serial.print("Historique non envoye");
        Serial.println(http.errorToString(codeHTTP));
    }
    http.end();
}


void processScan(int n) {
    //Vérification de l'état de connexion actuel
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    

    if (isConnected && !wasConnected) {
        Serial.println("Reconnexion au wifi");
        
        //envoi de l'historique
        if (nbPointsDansHistorique > 0) {
            envoyerHistoriqueChemin();
        }
    } 
    else if (!isConnected && wasConnected) {
        Serial.println("\nPerte de connexion");
    }
    
    // Mise à jour de l'état pour la prochaine boucle
    wasConnected = isConnected;

    if (isConnected) {
        //Mode en ligne
        String json = "{ \"measurements\": [";
        bool premier = true;
        int count = 0;
        
        for (int i = 0; i < n; ++i) { //formatage de l'envoie en JSON 
            if (WiFi.RSSI(i) < filtre_RSSI) continue; //application du premier filtre sur la puissance (dans mon cas tout ce qui est en dessous de -85dBm n'est pas prit en compte)
            if (Filtre_BSSID(WiFi.BSSID(i))) continue;//application du second filtre sur les adresses MAC (ici je ne prend pas les adresses mac volatiles)
            
            if (!premier) json += ","; // mise des virgules à partir de la deuxième valeure
            json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
            json += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\","; //permet de mettre le BSSID en format str
            json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
            json += "\"channel\":" + String(WiFi.channel(i)) + "}";
            premier = false;
            count++;
        }
        json += "] }";
        
        if (count > 0) sendLiveScan(json); // si il y a au moins 1 réseau alors l'envoyer 

    } else {
        // Mode hors ligne 
        Serial.print("Stockage du point");

        PointTemps point; //Création d'une variable avec la structure PoitTemps
        point.nbMesures = 0;  //on commence avec 0 mesure

        for (int i = 0; i < n; ++i) {
            if (WiFi.RSSI(i) < filtre_RSSI) continue;
            if (Filtre_BSSID(WiFi.BSSID(i))) continue;

            if (point.nbMesures >= NB_MAX_RESEAUX_PAR_POINT) {
                //on ne garde que les NB_MAX_RESEAUX_PAR_POINT premiers réseaux pour éviter de saturer l'ESP32
                break;
            }

            point.mesures[point.nbMesures].bssid = WiFi.BSSIDstr(i);
            point.mesures[point.nbMesures].rssi  = WiFi.RSSI(i);
            point.nbMesures++;
        }

        if (point.nbMesures > 0) {
            //Ajout du point dans l'historique
            if (nbPointsDansHistorique < NB_MAX_POINTS) {
                historiquePoints[nbPointsDansHistorique] = point;
                nbPointsDansHistorique++;
            } else {
                Serial.println("Mémoire pleine -> suppression du plus vieux point.");
                // Décale tout vers la gauche
                for (int k = 1; k < NB_MAX_POINTS; ++k) {
                    historiquePoints[k - 1] = historiquePoints[k];
                }
                // Place le nouveau point à la fin
                historiquePoints[NB_MAX_POINTS - 1] = point;
            }

            Serial.print("Mémoire: "); //affichage du nombre de point dans l'historique 
            Serial.print(nbPointsDansHistorique);
            Serial.print("/");
            Serial.println(NB_MAX_POINTS);
        } else {
            Serial.println("aucun reseau utile trouve"); //si aucun réseau n'est détecté on ne stocke rien
        }
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.setAutoReconnect(false); //désactive la reconnexion automatique
    WiFi.persistent(false);//se paramètre permet d'évite de réécrire dans la flash notre SSID et notre mot de passe lors de chaque reconnexion
    connectToWiFi();
}

void loop() {

    int n = WiFi.scanNetworks(); //Lance un scan pour trouver des réseaux 
    connectToWiFi();
    if (n > 0) { 
        processScan(n); //si il y a des réseaux alors ils sont traités par ma fonction processScan qui est plus haut
    } else {
        Serial.println("Aucun reseau trouve"); 
    }
    
    WiFi.scanDelete(); //Libère la mémoire de l'ESP
    delay(5000); //scan toutes les 5 secondes
}