// =====================================================================
//  ESP32 "NOEUD PRINCIPAL"
//  - Lit le capteur cardiaque DFRobot (pouls + SpO2) sur Wire1
//  - Envoie les mesures a Node-RED via MQTT (WiFi)
//  - Repond au noeud "Diagnostic" via I2C (adresse 0x55) avec un code erreur
// =====================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include "Wire.h"
#include <DFRobot_BloodOxygen_S.h>

// ---------- WiFi / MQTT (memes valeurs que tes autres ESP32) ----------
const char *ssid = "iot";
const char *password = "iotisis;";
const char *mqtt_server = "172.18.32.47";

WiFiClient espClient;
PubSubClient client(espClient);

// ---------- Capteur cardiaque ----------
DFRobot_BloodOxygen_S_I2C capteur(&Wire1, 0x57);
#define I2C_DEV_ADDR 0x55

// =====================================
// TABLE DES CODES ERREUR
// =====================================

#define ERR_NONE              0
#define ERR_WIFI_LOST         1
#define ERR_SENSOR_DISCONNECT 2
#define ERR_SENSOR_READ       3
#define ERR_TEMP_LIMIT        4
#define ERR_LOW_VOLTAGE       5
#define ERR_SERIAL_LOST       6
#define ERR_LOW_MEMORY        7
#define ERR_I2C               8
#define ERR_WATCHDOG          9
#define ERR_UNKNOWN           10

// Code erreur actuel (lu par le noeud Diagnostic)
volatile uint8_t errorCode = ERR_NONE;

// Variables de temps
unsigned long dernierEssaiMqtt = 0;
unsigned long derniereMesure = 0;
bool collecteDemarree = false;


// =====================================
// FONCTION DE RECEPTION I2C
// =====================================

void onReceive(int len) {
  Serial.printf("Commande reçue du Master : ");
  while (Wire.available()) {
    Serial.write(Wire.read());
  }
  Serial.println();
}


// =====================================
// FONCTION D'ENVOI I2C
// =====================================

void onRequest() {
  Wire.write(errorCode);
  Serial.print("Code erreur transmis : ");
  Serial.println(errorCode);
}


// =====================================
// WIFI + MQTT (non bloquant)
// Renvoie true si on peut publier
// =====================================

bool gererWifiMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;                              // pas de WiFi : on ne bloque pas
  }

  if (!client.connected()) {
    if (millis() - dernierEssaiMqtt < 5000) {  // on reessaie toutes les 5 s
      return false;
    }
    dernierEssaiMqtt = millis();
    Serial.print("Connexion MQTT...");
    if (!client.connect("ESP32Client_Principal")) {
      Serial.printf(" echec, code %d\n", client.state());
      return false;
    }
    Serial.println(" OK");
  }

  client.loop();
  return true;
}


// =====================================
// INITIALISATION
// =====================================

void setup() {
  Serial.begin(115200);

  // I2C avec le noeud Diagnostic (ce ESP32 = esclave 0x55)
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Wire.begin((uint8_t)I2C_DEV_ADDR);

  // I2C avec le capteur cardiaque
  Wire1.begin(21, 22, 100000);

  // WiFi + MQTT
  WiFi.begin(ssid, password);
  client.setServer(mqtt_server, 1883);

  Serial.println("ESP32 Noeud Principal initialise");
}


// =====================================
// BOUCLE PRINCIPALE
// =====================================

void loop() {
  // A appeler tres souvent pour garder la connexion MQTT
  bool reseauOK = gererWifiMqtt();

  // Une mesure toutes les 4 secondes (le capteur ne va pas plus vite)
  if (millis() - derniereMesure < 4000) {
    return;
  }
  derniereMesure = millis();

  // 1) Le capteur est-il branche ?
  Wire1.beginTransmission(0x57);
  if (Wire1.endTransmission() != 0) {
    errorCode = ERR_SENSOR_DISCONNECT;
    collecteDemarree = false;
    Serial.println("Capteur non detecte");
    return;
  }

  // 2) Premiere fois : on demarre la mesure
  if (!collecteDemarree) {
    capteur.sensorStartCollect();
    collecteDemarree = true;
    Serial.println("Capteur detecte. Pose ton doigt.");
    return;   // la 1re mesure arrivera au prochain tour
  }

  // 3) Lecture du pouls et de la SpO2
  capteur.getHeartbeatSPO2();
  int bpm  = (int)capteur._sHeartbeatSPO2.Heartbeat;
  int spo2 = (int)capteur._sHeartbeatSPO2.SPO2;
  Serial.printf("Pouls : %d BPM | SpO2 : %d %%\n", bpm, spo2);

  // Mesure valide ? (sans doigt, le capteur renvoie -1 ou une valeur absurde)
  bool mesureValide = (bpm > 0 && bpm < 250 && spo2 > 0 && spo2 <= 100);

  // 4) Code erreur pour le noeud Diagnostic
  if (WiFi.status() != WL_CONNECTED) {
    errorCode = ERR_WIFI_LOST;
    Serial.println("WiFi perdu");
  } else if (!mesureValide) {
    errorCode = ERR_SENSOR_READ;
    Serial.println("Mesure invalide (doigt mal pose ?)");
  } else {
    errorCode = ERR_NONE;
  }

  // 5) Envoi a Node-RED
  if (reseauOK && mesureValide) {
    client.publish("esp32/max30102/bpm",  String(bpm).c_str());
    client.publish("esp32/max30102/spo2", String(spo2).c_str());
    Serial.println("-> envoye a Node-RED");
  }
}
