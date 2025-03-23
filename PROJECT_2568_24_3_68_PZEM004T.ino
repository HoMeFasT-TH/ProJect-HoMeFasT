#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <PZEM004Tv30.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <EEPROM.h>
#include <TimeLib.h>
#include <WiFiManager.h>

// NTP Configuration
WiFiUDP udp;
NTPClient timeClient(udp, "pool.ntp.org", 25200, 60000);  //UTC+7 กำหนดเวลาในประเทศไทย

// Firebase Configuration
#define API_KEY "AIzaSyABBH31IS5sumVB4GmuUnQ2pUNNkWEh9xo" 
#define DATABASE_URL "https://mytemp-3579d-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_PROJECT_ID "mytemp-3579d"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Define PZEM004T RX/TX pins for SoftwareSerial
#define PZEM_RX_PIN D5
#define PZEM_TX_PIN D6

SoftwareSerial pzemSWSerial(PZEM_RX_PIN, PZEM_TX_PIN);
PZEM004Tv30 pzem(pzemSWSerial);

bool signupOK = false;

float DATAElectricityCost = 0;
float DATAcurrent = 0;
float electricityBill = 0.0;
unsigned long previousMillis = 0;
const long interval = 3600000; //ตอนนี้60นาที "3600000" กำหนดนับเวลาทุกๆ60นาที

#define EEPROM_SIZE 1
int previousMonth = 0;
int previousDay = 0;
float dailyEnergy = 0;

float calculateElectricityBill(float energy) { //กำหนดค่าไฟเบื่องต้นในESP8266
    float electricityBill = 0.0;
    if (energy <= 15) {
        electricityBill = energy * 2.3488;
    } else if (energy <= 25) {
        electricityBill = 15 * 2.3488 + (energy - 15) * 2.9882;
    } else if (energy <= 35) {
        electricityBill = 15 * 2.3488 + 10 * 2.9882 + (energy - 25) * 3.2405;
    } else if (energy <= 100) {
        electricityBill = 15 * 2.3488 + 10 * 2.9882 + 10 * 3.2405 + (energy - 35) * 3.6237;
    } else if (energy <= 150) {
        electricityBill = 15 * 2.3488 + 10 * 2.9882 + 10 * 3.2405 + 65 * 3.6237 + (energy - 100) * 3.7171;
    } else if (energy <= 400) {
        electricityBill = 15 * 2.3488 + 10 * 2.9882 + 10 * 3.2405 + 65 * 3.6237 + 50 * 3.7171 + (energy - 150) * 4.2218;
    } else {
        electricityBill = 15 * 2.3488 + 10 * 2.9882 + 10 * 3.2405 + 65 * 3.6237 + 50 * 3.7171 + 250 * 4.2218 + (energy - 400) * 4.4217;
    }
    return electricityBill;
}

void updateNTPTime() { //ดึงตามเวลาประเทศไทย
    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();
    if (epochTime > 0) {
        setTime(epochTime);
        Serial.println("NTP time updated successfully");
    } else {
        Serial.println("Failed to update NTP time");
    }
}

void setup() {
    Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);
    previousMonth = EEPROM.read(0);
    Serial.print("Previous Month from EEPROM: ");
    Serial.println(previousMonth);

    WiFiManager wifiManager;
    wifiManager.autoConnect("ESP-WiFi-PZEM004T");

    Serial.println("Connected to WiFi");
    Serial.println(WiFi.localIP());

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    if (Firebase.signUp(&config, &auth, "", "")) {
        Serial.println("Firebase connection successful");
        signupOK = true;
    } else {
        Serial.printf("Firebase sign-up error: %s\n", config.signer.signupError.message.c_str());
    }

    config.token_status_callback = tokenStatusCallback;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    timeClient.begin();
    timeClient.setTimeOffset(25200); // UTC+7
    updateNTPTime();
}

void loop() {
    updateNTPTime();

    Serial.print("Current time in Thailand: ");
    Serial.print(day());
    Serial.print("/");
    Serial.print(month());
    Serial.print("/");
    Serial.print(year());
    Serial.print(" ");
    Serial.println(timeClient.getFormattedTime());

    int currentMonth = month();
    int currentDay = day();
    Serial.print("Current Month: ");
    Serial.println(currentMonth);
    Serial.print("Previous Month: ");
    Serial.println(previousMonth);

    if (currentMonth != previousMonth) {
        delay(5000);
        pzem.resetEnergy();
        delay(1000);
        previousMonth = currentMonth;
        EEPROM.write(0, previousMonth);
        EEPROM.commit();
        }


    float voltage = pzem.voltage();
    if (isnan(voltage)) {
        voltage = 0;
        Serial.println("Error reading voltage, Voltage: 0 V");
    } else {
        Serial.print("Voltage: "); Serial.print(voltage); Serial.println(" V");
    }

    float current = pzem.current();
    if (isnan(current)) {
        current = 0;
        Serial.println("Error reading current, Current: 0 A");
    } else {
        Serial.print("Current: "); Serial.print(current); Serial.println(" A");
    }

    float power = pzem.power();
    if (isnan(power)) {
        power = 0;
        Serial.println("Error reading power, Power: 0 W");
    } else {
        Serial.print("Power: "); Serial.print(power); Serial.println(" W");
    }

    float pf = pzem.pf();
    float electricityBillTHB = (power / 1000) * 0.0167 * 4.18;
    Serial.print("Electricity Bill2: ");
    Serial.print(electricityBillTHB, 2);
    Serial.println(" THB");

    float energy = pzem.energy();
    if (isnan(energy)) {
        energy = 0;
        Serial.println("Error reading energy, Energy: 0 kWh");
    } else {
        Serial.print("Energy: "); Serial.print(energy, 3); Serial.println(" kWh");
    }

    float frequency = pzem.frequency();
    if (isnan(frequency)) {
        frequency = 0;
        Serial.println("Error reading frequency, Frequency: 0 Hz");
    } else {
        Serial.print("Frequency: "); Serial.print(frequency, 1); Serial.println(" Hz");
    }

    if (isnan(pf)) {
        pf = 0;
        Serial.println("Error reading power factor, PF: 0");
    } else {
        Serial.print("PF: "); Serial.println(pf);
    }

    float powerall = (energy * 1000);
    Serial.print("powerall: ");
    Serial.print(powerall, 2);
    Serial.println(" W");

    electricityBill = calculateElectricityBill(energy);

    DATAElectricityCost += electricityBillTHB;
    Serial.print("Accumulated ElectricityCost: ");
    Serial.println(DATAElectricityCost, 2);

    DATAcurrent += current;
    Serial.print("Accumulated Current: ");
    Serial.println(DATAcurrent, 2);

    dailyEnergy += energy;

    if (Firebase.ready() && signupOK) {
        if (Firebase.RTDB.setFloat(&fbdo, "PZEM004T/Voltage", voltage)) {
            Serial.println("Voltage sent successfully");
        } else {
            Serial.println("Failed to send Voltage: " + fbdo.errorReason());
        }

        if (Firebase.RTDB.setFloat(&fbdo, "PZEM004T/Irms", current)) {
            Serial.println("Current sent successfully");
        } else {
            Serial.println("Failed to send Current: " + fbdo.errorReason());
        }

        if (Firebase.RTDB.setFloat(&fbdo, "PZEM004T/Power", power)) {
            Serial.println("Power sent successfully");
        } else {
            Serial.println("Failed to send Power: " + fbdo.errorReason());
        }

        if (Firebase.RTDB.setFloat(&fbdo, "PZEM004T/ElectricityCost", electricityBillTHB)) {
            Serial.println("ElectricityCost sent successfully");
        } else {
            Serial.println("Failed to send ElectricityCost: " + fbdo.errorReason());
        }
        
        if (Firebase.RTDB.setFloat(&fbdo, "PZEM004T/Energy", energy)) {
            Serial.println("Energy sent successfully");
        } else {
            Serial.println("Failed to send Energy: " + fbdo.errorReason());
        }

        if (Firebase.RTDB.setFloat(&fbdo, "PZEM004T/Frequency", frequency)) {
            Serial.println("Frequency sent successfully");
        } else {
            Serial.println("Failed to send Frequency: " + fbdo.errorReason());
        }

        if (Firebase.RTDB.setFloat(&fbdo, "PZEM004T/PF", pf)) {
            Serial.println("PF sent successfully");
        } else {
            Serial.println("Failed to send PF: " + fbdo.errorReason());
        }

        // Send data to Firestore
        String documentPath1 = "emonData/PZEM004T"; //String documentPath1 = "emonData/PZEM004T1";
        FirebaseJson content1;
        content1.set("fields/Voltage/doubleValue", voltage);
        content1.set("fields/Irms/doubleValue", current);
        content1.set("fields/Power/doubleValue", power);
        content1.set("fields/ElectricityCost/doubleValue", electricityBillTHB);
        content1.set("fields/Energy/doubleValue", energy);
        content1.set("fields/Frequency/doubleValue", frequency);
        content1.set("fields/PF/doubleValue", pf);
        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath1.c_str(), content1.raw(), "Voltage,Irms,Power,ElectricityCost,Energy,Frequency,PF")) {
            Serial.printf("PZEM004T Firestore update ok\n%s\n\n", fbdo.payload().c_str());
        } else {
            Serial.println("PZEM004T Firestore update failed: " + fbdo.errorReason());
        }

        // Send data to Firestore for "emonDataBack/Times" every 20 minutes
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;

            String documentPath2 = "emonDataBack/" + String(year()) + "-" + String(month()) + "-" + String(day()) + "_" + timeClient.getFormattedTime(); //emonDataBack3
            FirebaseJson content2;
            content2.set("fields/Voltage/doubleValue", voltage);
            content2.set("fields/Irms/doubleValue", current);
            content2.set("fields/Power/doubleValue", power);
            content2.set("fields/ElectricityCost/doubleValue", electricityBillTHB);
            content2.set("fields/Energy/doubleValue", energy);
            content2.set("fields/Frequency/doubleValue", frequency);
            content2.set("fields/PF/doubleValue", pf);

            if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath2.c_str(), content2.raw(), "Voltage,Irms,Power,ElectricityCost,Energy,Frequency,PF")) {
                Serial.printf("PZEM004Tx2 Firestore update ok\n%s\n\n", fbdo.payload().c_str());
            } else {
                Serial.println("PZEM004Tx2 Firestore update failed: " + fbdo.errorReason());
            }
        }

        // Read DATAcurrent from Firestore
        String documentPath3 = "emonDataAll/HOME"; //String documentPath3 = "emonDataAll3/HOME";
        float DATAcurrentFromFirestore = 0;
        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath3.c_str())) {
            FirebaseJson& json = fbdo.jsonObject();
            FirebaseJsonData jsonData;
            if (json.get(jsonData, "fields/DATAcurrent/doubleValue")) {
                DATAcurrentFromFirestore = jsonData.doubleValue;
            }
        } else {
            Serial.println("Failed to get DATAcurrent: " + fbdo.errorReason());
        }

        // Update DATAcurrent
        DATAcurrentFromFirestore += current;

        // Send updated data to Firestore
        FirebaseJson content3;
        content3.set("fields/PowerALL/doubleValue", powerall);
        content3.set("fields/energy/doubleValue", energy);
        content3.set("fields/DATAElectricityCost/doubleValue", electricityBill);

        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath3.c_str(), content3.raw(), "PowerALL,energy,DATAElectricityCost")) {
            Serial.printf("emonDataAll Firestore update ok\n%s\n\n", fbdo.payload().c_str());
        } else {
            Serial.println("emonDataAll Firestore update failed: " + fbdo.errorReason());
        }

        // Store daily energy usage at the end of the day
        if (currentDay != previousDay) {
            String documentPathDaily = "dailyEnergyUsage/" + String(year()) + "-" + String(month()) + "-" + String(previousDay);
            FirebaseJson contentDaily;
            contentDaily.set("fields/dailyEnergy/doubleValue", energy);
            contentDaily.set("fields/DATAElectricityCost/doubleValue", electricityBill);

            if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPathDaily.c_str(), contentDaily.raw(), "dailyEnergy,DATAElectricityCost")) {
                Serial.printf("Daily energy usage update ok\n%s\n\n", fbdo.payload().c_str());
            } else {
                Serial.println("Daily energy usage update failed: " + fbdo.errorReason());
            }

            // Reset daily energy and update previousDay
            dailyEnergy = 0;
            previousDay = currentDay; 
        }
    }
    delay(10000); // Wait 10 seconds before next iteration
}