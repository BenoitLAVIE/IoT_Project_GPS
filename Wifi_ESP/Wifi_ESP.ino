#include <WiFi.h>
#include <HTTPClient.h>

const char* WIFI_SSID     = "Benoit's Galaxy S21 FE 5G";
const char* WIFI_PASSWORD = "benoit12";
const char* SERVER_URL    = "http://192.168.59.162:8000/locate_me";

const int filtre_RSSI = -85;

String Localisation_actuelle = "";

void connectToWiFi() {
    Serial.print("Connexion au WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" Connecte");
}


void askForLocation() {
    Serial.println("ATTENTE DE CONFIGURATION");
    while (Serial.available() == 0) {
        delay(100); 
    }
    Localisation_actuelle = Serial.readStringUntil('\n'); // Lit jusqu'à la touche Entrée
    Localisation_actuelle.trim(); //Enleve les espaces et retours a la ligne parasites
    delay(2000);
}


bool Filtre_BSSID(uint8_t* bssid) {
    // On regarde le premier octet du BSSID si le 2eme bit est a 1, c'est une adresse locale
    return (bssid[0] & 0x02); 
}

void sendScanToServer(int n) {
    if (WiFi.status() != WL_CONNECTED) return; // Vérifie la connexion WiFi

    String json = "{";
    //json += "\"location\": \"" + Localisation_actuelle + "\","; 
    json += "\"measurements\": [";
    bool premier = true; 
    int count = 0;

    for (int i = 0; i < n; ++i) {
        int rssi = WiFi.RSSI(i);
        String ssid = WiFi.SSID(i);
        
        // Récupération du BSSID sous forme brute
        uint8_t* bssid_brut = WiFi.BSSID(i);

        if (rssi < filtre_RSSI) continue;

        if (ssid.length() == 0) continue;

        if (Filtre_BSSID(bssid_brut)) {
            continue;
        }

        if (!premier) json += ",";
        
        json += "{";
        
        json += "\"ssid\":\""   + ssid                      + "\",";
        json += "\"bssid\":\""  + String(WiFi.BSSIDstr(i))  + "\",";
        json += "\"rssi\":"     + String(rssi)              + ",";
        json += "\"channel\":"  + String(WiFi.channel(i));
        json += "}";
        
        premier = false;
        count++;
    }

    json += "] }";

    if (count == 0) return;


    HTTPClient http; //on redéfinie la variable http sous la structure HTTPClient afin d'utiliser les fonctions de la librairie HTTP
    http.begin(SERVER_URL); //connexion au serveur à la route définie par SERVER_URL
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

void setup() {
    Serial.begin(115200);
    //askForLocation();
    connectToWiFi();
}

void loop() {
    //if (Localisation_actuelle == "") {
       //askForLocation();
    
    //}
    int n = WiFi.scanNetworks();
    if (n > 0) sendScanToServer(n);
    WiFi.scanDelete();
    delay(5000);
}