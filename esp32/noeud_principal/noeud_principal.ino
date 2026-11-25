// =====================================================================
//  NOEUD PRINCIPAL
//  - Capteur Gravity SEN0518 (MAX30102 : BPM + SpO2) sur I2C bus 1 (IO25/IO26)
//  - Publie les mesures en JSON sur MQTT (broker du Raspberry)
//  - Esclave I2C (adresse 0x55, IO21/IO22) pour le noeud de diagnostic
// =====================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "DFRobot_BloodOxygen_S.h"

// ---------------- Réseau ----------------
const char *ssid        = "iot";
const char *password    = "iotisis;";
const char *mqtt_server = "172.18.32.47";   // IP du Raspberry (broker)

const char *TOPIC_CAPTEUR = "principal/capteur";  // {"bpm":72,"spo2":98,"temp":31.5,"valide":true}
const char *TOPIC_STATUT  = "principal/statut";   // {"code":0,"message":"...","rssi":-60,...}
const char *TOPIC_BPM     = "esp32/max30102/bpm";  // "72"  (utilisé par le flow Node-RED)
const char *TOPIC_SPO2    = "esp32/max30102/spo2"; // "98"

// ---------------- Capteur (bus I2C n°1) ----------------
#define SENSOR_ADDR 0x57
#define SENSOR_SDA  25
#define SENSOR_SCL  26
TwoWire I2C_2 = TwoWire(1);
DFRobot_BloodOxygen_S_I2C MAX30102(&I2C_2, SENSOR_ADDR);

// ---------------- Liaison diagnostic (bus I2C n°0, esclave) ----------------
#define I2C_DEV_ADDR 0x55   // IO21 = SDA, IO22 = SCL
#define DIAG_LEN     4      // [code erreur, SpO2, BPM, flags]

// Flags (octet 3 de la trame de diagnostic)
#define FLAG_WIFI    0x01
#define FLAG_MQTT    0x02
#define FLAG_CAPTEUR 0x04
#define FLAG_MESURE  0x08

// ---------------- Table des codes erreur (identique au noeud diagnostic) ----------------
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
#define ERR_MQTT_LOST         11

const char *getErrorMessage(uint8_t code) {
  switch (code) {
    case ERR_NONE:              return "Fonctionnement nominal : acquisition capteur en cours";
    case ERR_WIFI_LOST:         return "Impossible de se connecter au WiFi";
    case ERR_SENSOR_DISCONNECT: return "Probleme liaison capteur (capteur non connecte)";
    case ERR_SENSOR_READ:       return "Mesure invalide (doigt absent ou mal place)";
    case ERR_TEMP_LIMIT:        return "Temperature hors limites";
    case ERR_LOW_VOLTAGE:       return "Tension alimentation insuffisante";
    case ERR_SERIAL_LOST:       return "Perte communication serie";
    case ERR_LOW_MEMORY:        return "Memoire insuffisante";
    case ERR_I2C:               return "Erreur communication I2C";
    case ERR_WATCHDOG:          return "Watchdog declenche";
    case ERR_UNKNOWN:           return "Erreur systeme inconnue";
    case ERR_MQTT_LOST:         return "Broker MQTT injoignable";
    default:                    return "Code erreur non repertorie";
  }
}

// ---------------- Seuils de diagnostic ----------------
const float    TEMP_MIN = 0.0;
const float    TEMP_MAX = 50.0;
const uint32_t HEAP_MIN = 20000;   // octets

// ---------------- Variables ----------------
WiFiClient espClient;
PubSubClient client(espClient);

volatile uint8_t diagBuffer[DIAG_LEN] = {ERR_NONE, 0, 0, 0};

bool  sensorOk = false;
int   lastSpo2 = -1;
int   lastBpm  = -1;
float lastTemp = NAN;

uint8_t errorCode         = ERR_NONE;
uint8_t lastPublishedCode = 255;

unsigned long lastSensorCheck = 0;
unsigned long lastRead        = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastStatus      = 0;

// =====================================================================
//  I2C esclave : le noeud diagnostic demande DIAG_LEN octets
// =====================================================================
void onRequest() {
  uint8_t copy[DIAG_LEN];
  for (int i = 0; i < DIAG_LEN; i++) copy[i] = diagBuffer[i];
  Wire.write(copy, DIAG_LEN);
}

// =====================================================================
//  Capteur
// =====================================================================
bool sensorPresent() {
  I2C_2.beginTransmission(SENSOR_ADDR);
  return I2C_2.endTransmission() == 0;
}

// Détecte débranchement / rebranchement du capteur sans bloquer
void checkSensor() {
  bool present = sensorPresent();

  if (present && !sensorOk) {
    if (MAX30102.begin()) {
      MAX30102.sensorStartCollect();
      sensorOk = true;
      lastRead = 0;
      Serial.println("Capteur initialise, debut des mesures");
    }
  } else if (!present && sensorOk) {
    sensorOk = false;
    Serial.println("Capteur deconnecte !");
  }

  if (!sensorOk) {
    lastSpo2 = -1;
    lastBpm  = -1;
    lastTemp = NAN;
  }
}

void readSensor() {
  MAX30102.getHeartbeatSPO2();
  lastSpo2 = MAX30102._sHeartbeatSPO2.SPO2;
  lastBpm  = MAX30102._sHeartbeatSPO2.Heartbeat;
  lastTemp = MAX30102.getTemperature_C();

  bool valide = (lastSpo2 > 0 && lastBpm > 0);

  Serial.printf("SpO2 : %d %% | BPM : %d | Temp carte : %.1f C | %s\n",
                lastSpo2, lastBpm, lastTemp, valide ? "valide" : "invalide");

  if (client.connected()) {
    char temp[12];
    if (isnan(lastTemp)) strcpy(temp, "null");
    else snprintf(temp, sizeof(temp), "%.1f", lastTemp);

    char json[128];
    snprintf(json, sizeof(json),
             "{\"bpm\":%d,\"spo2\":%d,\"temp\":%s,\"valide\":%s}",
             lastBpm, lastSpo2, temp, valide ? "true" : "false");
    client.publish(TOPIC_CAPTEUR, json);

    // Valeurs seules pour les graphiques et l'alarme du flow Node-RED
    // (uniquement si la mesure est valide, pour ne pas tracer de -1)
    if (valide) {
      char val[8];
      snprintf(val, sizeof(val), "%d", lastBpm);
      client.publish(TOPIC_BPM, val);
      snprintf(val, sizeof(val), "%d", lastSpo2);
      client.publish(TOPIC_SPO2, val);
    }
  }
}

// =====================================================================
//  Diagnostic : calcule le code erreur prioritaire
// =====================================================================
uint8_t computeErrorCode() {
  if (!sensorOk)                       return ERR_SENSOR_DISCONNECT;
  if (WiFi.status() != WL_CONNECTED)   return ERR_WIFI_LOST;
  if (!client.connected())             return ERR_MQTT_LOST;
  if (lastSpo2 <= 0 || lastBpm <= 0)   return ERR_SENSOR_READ;
  if (!isnan(lastTemp) && (lastTemp < TEMP_MIN || lastTemp > TEMP_MAX))
                                       return ERR_TEMP_LIMIT;
  if (ESP.getFreeHeap() < HEAP_MIN)    return ERR_LOW_MEMORY;
  return ERR_NONE;
}

void updateDiagBuffer() {
  uint8_t flags = 0;
  if (WiFi.status() == WL_CONNECTED) flags |= FLAG_WIFI;
  if (client.connected())            flags |= FLAG_MQTT;
  if (sensorOk)                      flags |= FLAG_CAPTEUR;
  if (lastSpo2 > 0 && lastBpm > 0)   flags |= FLAG_MESURE;

  diagBuffer[0] = errorCode;
  diagBuffer[1] = (lastSpo2 > 0) ? (uint8_t)min(lastSpo2, 255) : 0;
  diagBuffer[2] = (lastBpm  > 0) ? (uint8_t)min(lastBpm,  255) : 0;
  diagBuffer[3] = flags;
}

void publishStatus() {
  if (!client.connected()) return;
  char json[200];
  snprintf(json, sizeof(json),
           "{\"code\":%u,\"message\":\"%s\",\"rssi\":%d,\"heap\":%u,\"uptime\":%lu}",
           errorCode, getErrorMessage(errorCode), WiFi.RSSI(),
           ESP.getFreeHeap(), millis() / 1000);
  client.publish(TOPIC_STATUT, json, true);   // retained
  lastPublishedCode = errorCode;
}

// =====================================================================
//  MQTT
// =====================================================================
void handleMqtt(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!client.connected()) {
    if (now - lastMqttAttempt < 5000) return;
    lastMqttAttempt = now;

    // Last Will : le broker publie "hors ligne" si le noeud disparait
    if (client.connect("ESP32_Principal", TOPIC_STATUT, 0, true,
                       "{\"code\":-1,\"message\":\"Noeud principal hors ligne\"}")) {
      Serial.println("MQTT connecte");
      lastPublishedCode = 255;   // force la republication du statut
    } else {
      Serial.printf("Connexion MQTT impossible, code %d\n", client.state());
      return;
    }
  }
  client.loop();
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);

  // Bus capteur
  I2C_2.begin(SENSOR_SDA, SENSOR_SCL, 100000);
  checkSensor();

  // Bus diagnostic (esclave)
  Wire.onRequest(onRequest);
  Wire.begin((uint8_t)I2C_DEV_ADDR);

  // Réseau
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  client.setServer(mqtt_server, 1883);

  lastMqttAttempt = millis() - 5000UL;
  Serial.println("Noeud principal initialise");
}

void loop() {
  const unsigned long now = millis();

  // Vérifie la présence du capteur toutes les secondes
  if (now - lastSensorCheck >= 1000) {
    lastSensorCheck = now;
    checkSensor();
  }

  // Le capteur met à jour ses données toutes les 4 s
  if (sensorOk && (lastRead == 0 || now - lastRead >= 4000)) {
    lastRead = now;
    readSensor();
  }

  handleMqtt(now);

  // Mise à jour du diagnostic
  uint8_t newCode = computeErrorCode();
  if (newCode != errorCode) {
    errorCode = newCode;
    Serial.printf("Nouvel etat : [%u] %s\n", errorCode, getErrorMessage(errorCode));
  }
  updateDiagBuffer();

  if (errorCode != lastPublishedCode || now - lastStatus >= 30000) {
    lastStatus = now;
    publishStatus();
  }
}
