/*
 Station météo à base d'un ESP8266, et envoi des données vers Domoticz via MQTT.
 Création Dominique PAUL.
 Dépot Github : https://github.com/DomoticDIY/.........
 Chaine YouTube du Tuto Vidéo : https://www.youtube.com/c/DomoticDIY
  
 Bibliothéques nécessaires, et version utilisées :
  - pubsubclient v2.7.0 : https://github.com/knolleary/pubsubclient
  - ArduinoJson v6.15.0 : https://github.com/bblanchon/ArduinoJson
  - ESP8266Wifi v1.0.0
  - ArduinoOTA v1.0.0
  - Adafruit unified sensor v 1.1.2
  - Adafruit BME280 v2.0.2
  - Adafruit VEML6070 Library v1.0.4
  - DHT sensor library by Adafruit v1.4.0

 Installer le gestionnaire de carte ESP8266 version 2.7.1 
 Si besoin : URL à ajouter pour le Bord manager : http://arduino.esp8266.com/stable/package_esp8266com_index.json
 
 Adaptation pour reconnaissance dans Domoticz :
 Dans le fichier PubSubClient.h : La valeur du paramètre "MQTT_MAX_PACKET_SIZE" doit être augmentée à 512 octets. Cette définition se trouve à la ligne 26 du fichier.
 Sinon cela ne fonctionne pas avec Domoticz
 
 Pour prise en compte du matériel :
 Installer si besoin le Driver USB CH340G : https://wiki.wemos.cc/downloads
 dans Outils -> Type de carte : generic ESP8266 module
  Flash mode 'QIO' (régle générale, suivant votre ESP, si cela ne fonctionne pas, tester un autre mode.
  Flash size : 1M (no SPIFFS)
  Port : Le port COM de votre ESP vu par windows dans le gestionnaire de périphériques.
*/

// #include "connect.h"
const char* ssid = "_MON_SSID_";                   // SSID du réseau Wifi
const char* password = "_MOT_DE_PASSE_WIFI_";      // Mot de passe du réseau Wifi.
const char* mqtt_server = "_IP_DU_BROKER_";         // Adresse IP ou DNS du Broker.
const int mqtt_port = 1883;                         // Port du Brocker MQTT
const char* mqtt_login = "_LOGIN_";                 // Login de connexion à MQTT.
const char* mqtt_password = "_PASSWORD_";           // Mot de passe de connexion à MQTT.
// ------------------------------------------------------------
// Variables de configuration :
const char* topicIn     = "domoticz/out";             // Nom du topic envoyé par Domoticz
const char* topicOut    = "domoticz/in";              // Nom du topic écouté par Domoticz
// ------------------------------------------------------------
// Variables et constantes utilisateur :
String nomModule  = "Station Météo";                  // Nom usuel de ce module. Sera visible uniquement dans les Log Domoticz.
unsigned long t_lastActionCapteur = 0;                // enregistre le Time de la dernière intérogation.
const long t_interoCapteur = 60000;                   // Valeur de l'intervale entre 2 relevés.
// Variables techniques :
#define DHTPIN D4                                     // PIN digital de connexion DATA du DHT22
#define DHTTYPE DHT22                                 // Définition du type de DHT utilisé
// Variables Index Domoticz. (Mettre IDX = 0 si pas de device).
int idxBME280         = 0;                           // Index du capteur BME280
int idxPA             = 0;                            // Index du capteur BME ou BMP 280, uniquement pour la pression atmosphérique.
int correctionBME280  = 11;                           // Correction de la PA en fonction de l'altitude de la station (+ 1hPA tous les 8 m).
int idxAltitude       = 0;                            // Index du Device de distance.
int idxVEML6070       = 0;                            // Index du Device de l'indice UV.
int idxDHT22          = 63;                           // Index du Device de température et humidité.
int idxTempRessenti   = 65;                           // Index du Device de température ressenti (fonction de la température et de l'humidité du DHT22).

// On intégre les bibliothéques necessaire à la mise à jour via OTA.
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
// On intégre les librairies necessaires à la connexion Wifi, au MQTT, et à JSON, necessaire pour le MQTT.
#include <PubSubClient.h>
#include <ArduinoJson.h>
// On intégre les librairies necessaires aux Capteurs.
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_VEML6070.h>
#include <DHT.h>

// Initialisation du Wifi et du MQTT.
WiFiClient espClient;
PubSubClient client(espClient);

// Initialisation du BME280
Adafruit_BME280 bme;
// float temperature, humidity, pressure, altitude;
#define SEALEVELPRESSURE_HPA (1013.25)
// Initialisation UV
Adafruit_VEML6070 uv = Adafruit_VEML6070();
// Initialisation du DHT
DHT dht(DHTPIN, DHTTYPE);


void setup() {
  Serial.begin(115200);                           // On initialise la vitesse de transmission de la console.
  setup_wifi();                                   // Connexion au Wifi
  initOTA();                                      // Initialisation de l'OTA
  client.setServer(mqtt_server, mqtt_port);       // On défini la connexion MQTT  
  init_capteurs();                                // Initialisation des Capteurs de relevés
}

void loop() {
  ArduinoOTA.handle();                            // On verifie si une mise a jour OTA nous est envoyée. Si OUI, la lib ArduinoOTA se charge de faire la MAJ.
  unsigned long currentMillis = millis();         // On enregistre le Time courant
  
  // On s'assure que MQTT est bien connecté.
  if (!client.connected()) {
    // MQTT déconnecté, on reconnecte.
    Serial.println("MQTT déconnecté, on reconnecte !");
    reconnect();
  } else {
    // On vérifie que l'intervale de relevé des capteurs est atteint. (60s)
    if (currentMillis - t_lastActionCapteur >= t_interoCapteur) {
      // MQTT connecté, on exécute les traitements
      getDataBME280();    // Relevé des données du BME280, et envoi à Domoticz
      getDataVEML6070();  // Relevé des données du VEML6070, et envoi à Domoticz
      getDataDHT();       // Relevé des données du DHT, et envoi à Domoticz
      
      // Traitement effectué, on met à jour la valeur du dernier traitement.
      t_lastActionCapteur = currentMillis;
    }
  }
  
}



// INITIALISATION des Capteurs
// ***************************
void init_capteurs() {
  bme.begin(0x76);          // 0x76 = I2C adresse
  uv.begin(VEML6070_1_T);   // passer dans la constante de temps d'intégration
  dht.begin();              // Initialisation du capteur DHT
  
}


/* 
 * ************** */
/*  Les Fonctions
 * ************** */
 void getDataBME280() {
  // Relevé et envoi du BM280 (Pression atmosphérique)
  String temperature  = String(bme.readTemperature());
  String humidity     = String(bme.readHumidity());
  int pressure        = (bme.readPressure() / 100.0F) + correctionBME280;
  String altitude     = String(bme.readAltitude(SEALEVELPRESSURE_HPA) * 100);
  
  int bar_for         = 0;
  // Calcul de BAR_FOR (0 = No info; 1 = Soleil; 6 = Partiellement nuageux; 3 = Variable; 4 = Pluie)
  if (pressure < 980) { bar_for = 4; }
  else if (pressure < 1000) { bar_for = 6; }
  else if (pressure < 1020) { bar_for = 3; } 
  else if (pressure >= 1020) { bar_for = 1; }
  else { bar_for = 0; }
  // Envoi des données à Domoticz via MQTT
  if ( idxBME280 != 0) {
    String svalue     = temperature + ";" + humidity + ";0;" + String(pressure) + ";" + String(bar_for);          // svalue = TEMP;HUM;HUM_STAT;BAR;BAR_FOR
    Serial.print("BME280 : svalue = "); Serial.print(svalue); Serial.print(" (TEMP;HUM;HUM_STAT;BAR;BAR_FOR)"); // Message pour débug en console
    SendData("udevice", idxBME280, 0, svalue); 
  }
  if ( idxPA != 0) {
    String svalue     = String(pressure) + ";" + String(bar_for);                                                 // svalue = BAR;BAR_FOR
    Serial.print("BME280 : svalue = "); Serial.print(svalue); Serial.print(" (BAR;BAR_FOR)");                   // Message pour débug en console
    SendData("udevice", idxPA, 0, svalue); 
  }
  if ( idxAltitude != 0) {
    Serial.print("Altitude : svalue = "); Serial.print(altitude); Serial.print(" (Distance en cm)");
    SendData("udevice", idxAltitude, 0, altitude);        
  }
}


void getDataVEML6070() {
  uint16_t valeur = uv.readUV();

   if ( idxVEML6070 != 0) {
    String svalue =  String(valeur) + ";0" ;
    Serial.print("VEML6070 : svalue = "); Serial.print(svalue); Serial.print(" (Indice + un 0)");
    SendData("udevice", idxVEML6070, 0, svalue);
  }
}


void getDataDHT() {
  // Relevé et envoi du DHT11 ou DHT22 (Température et humidité)
  float hum   = dht.readHumidity();                         // Lecture de l'humidité
  float temp  = dht.readTemperature();                      // Lecture de la temperature en degrés Celsius (par défaut)

  // On vérifie que l'on a bien reçu des données des capteurs
  if (isnan(hum) || isnan(temp)) {
    // Les valeurs ne sont pas de données, on sort de la boucle
    return;
  }

  // On établi le degré de confort
  int hum_status = 0;
  if ((hum >= 45) and (hum <= 70))      { hum_status = 1; } // confortable
  else if ((hum >= 30) and (hum < 45))  { hum_status = 0; } // Normal
  else if (hum < 30)                    { hum_status = 2; } // sec
  else if (hum > 70)                    { hum_status = 3; } // humide

  if ( idxDHT22 != 0) {
    String svalue =  String(temp,1) + ";" + String(hum,0) + ";" + String(hum_status);
    Serial.print("DHT22 : svalue = "); Serial.print(svalue); Serial.print(" (TEMP;HUM;HUM_STAT)");
    SendData("udevice", idxDHT22, 0, svalue);
  }

  if ( idxTempRessenti != 0) {
    String svalue2 = String(dht.computeHeatIndex(temp, hum, false),1);   // Calcul de l'indice de chaleur en Celsius - Température ressenti (isFahreheit = false)
    Serial.print("DHT22 Temp Ressenti : svalue = "); Serial.print(svalue2); Serial.print(" (TEMP)");
    SendData("udevice", idxTempRessenti, 0, svalue2);
  } 
}


// CONNEXION WIFI
// **************
void setup_wifi() {
  // Connexion au réseau Wifi
  delay(10);
  Serial.println();
  Serial.print("Connection au réseau : ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);            // Passage de la puce en mode client
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    // Tant que l'on est pas connecté, on boucle.
    delay(500);
    Serial.print(".");
  }
  // Initialise la séquence Random
  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connecté");
  Serial.print("Addresse IP : ");
  Serial.println(WiFi.localIP());
}


// INITIALISATION Arduino OTA
// **************************
void initOTA() {
  /* Port par defaut = 8266 */
  // ArduinoOTA.setPort(8266);

  /* Hostname, par defaut = esp8266-[ChipID] */
  ArduinoOTA.setHostname("maStationMeteo");

  /* Pas d'authentication par defaut */
  // ArduinoOTA.setPassword("admin");

  /* Le mot de passe peut également être défini avec sa valeur md5 */
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
  
  // code à exécuter au démarrage de la mise à jour
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: si vous mettez à jour SPIFFS, démonter SPIFFS à l'aide de SPIFFS.end ()
    Serial.println("Début de update " + type);
  });
  
  // code à exécuter à la fin de la mise à jour
  ArduinoOTA.onEnd([]() {
    Serial.println("\nFin");
  });
  
  // code à exécuter pendant la mise à jour
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progression : %u%%\r", (progress / (total / 100)));
  });
  
  // code à exécuter en cas d'erreur de la mise à jour
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Erreur[%u] : ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Authentification Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Exécution Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connexion Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Réception Failed");
    else if (error == OTA_END_ERROR) Serial.println("Fin Failed");
  });
  
  ArduinoOTA.begin();
  Serial.println("Prêt");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}


// CONNEXION MQTT
// **************
void reconnect() {
  
  // Boucle jusqu'à la connexion MQTT
  while (!client.connected()) {
    Serial.print("Tentative de connexion MQTT...");
    // Création d'un ID client aléatoire
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    
    // Tentative de connexion
    if (client.connect(clientId.c_str(), mqtt_login, mqtt_password)) {
      Serial.println("connecté");
      
      // Connexion effectuée, publication d'un message...
      String message = "Connexion MQTT de "+ nomModule + " réussi sous référence technique : " + clientId + ".";
      
      // Création de la chaine JSON6
      DynamicJsonDocument doc(256);
      // On renseigne les variables.
      doc["command"] = "addlogmessage";
      doc["message"] = message;
      // On sérialise la variable JSON
      String messageOut;
      serializeJson(doc, messageOut);
      
      // Convertion du message en Char pour envoi dans les Log Domoticz.
      char messageChar[messageOut.length()+1];
      messageOut.toCharArray(messageChar,messageOut.length()+1);
      client.publish(topicOut, messageChar);
        
      // On souscrit (écoute)
      client.subscribe("#");
    } else {
      Serial.print("Erreur, rc=");
      Serial.print(client.state());
      Serial.println(" prochaine tentative dans 2s");
      // Pause de 2 secondes
      delay(2000);
    }
  }
}


// ENVOI DES DATAS.
// ***************
void SendData (String command, int idxDevice, int nvalue, String svalue) {
  // Création de la chaine JSON6
  DynamicJsonDocument doc(256);
  // On renseigne les variables.
  doc["command"]  = command;
  doc["idx"]      = idxDevice;
  doc["nvalue"]   = nvalue;
  doc["svalue"]   = svalue;
  
  // On sérialise la variable JSON
  String messageOut;
  serializeJson(doc, messageOut);
      
  // Convertion du message en Char pour envoi dans les Log Domoticz.
  char messageChar[messageOut.length()+1];
  messageOut.toCharArray(messageChar,messageOut.length()+1);
  client.publish(topicOut, messageChar);
  // Pause de 1 secondes
  delay(1000);
  Serial.println("\t -> Message envoyé à Domoticz");
}
 
