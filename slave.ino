
#include "Wire.h"
#include <DFRobot_BloodOxygen_S.h>

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
#define ERR_WATCHDOG           9
#define ERR_UNKNOWN           10

// Code erreur actuel
uint8_t errorCode = ERR_NONE;


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
// INITIALISATION
// =====================================

void setup() {

  Serial.begin(115200);

  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);

  Wire.begin((uint8_t)I2C_DEV_ADDR);

  Serial.println("ESP32 Slave initialise");
  Wire1.begin(21, 22, 100000);  // D2 = données, D3 = horloge
}


// =====================================
// BOUCLE PRINCIPALE
// =====================================

void loop() {
  static bool collecteDemarree = false;

  Wire1.beginTransmission(0x57);
  if (Wire1.endTransmission() != 0) {
    errorCode = ERR_SENSOR_DISCONNECT;
    collecteDemarree = false;
    Serial.println("Capteur non detecte");
    delay(1000);
    return;
  }

  if (!collecteDemarree) {
    capteur.sensorStartCollect();
    collecteDemarree = true;
    errorCode = ERR_NONE;
    Serial.println("Capteur detecte. Pose ton doigt.");
    delay(4000);
    return;
  }

  capteur.getHeartbeatSPO2();
  Serial.printf("Pouls : %ld BPM | SpO2 : %d %%\n",
                (long)capteur._sHeartbeatSPO2.Heartbeat,
                (int)capteur._sHeartbeatSPO2.SPO2);
  delay(4000);
}