#include <esp_now.h>
#include <WiFi.h>

// ================= CONFIGURATION =================
#define NODE_ID 1            // Change this to 1, 2, 3, or 4 for each node
#define MQ6_ANALOG_PIN A0    // Analog pin connected to MQ6
#define MQ6_DIGITAL_PIN 2    // Digital pin connected to MQ6
#define SEND_INTERVAL 30000  // 30 seconds interval

// Main Hub MAC Address (Replace with your actual Hub MAC Address)
uint8_t hubAddress[] = {0x24, 0x0A, 0xC4, 0xXX, 0xXX, 0xXX}; 
// =================================================

typedef struct struct_message {
    int node_id;
    int ppm;
    bool digital_leak;
} struct_message;

struct_message myData;
unsigned long lastTime = 0;
esp_now_peer_info_t peerInfo;

// Simple function to map raw analog data to rough PPM range (200 - 10000)
int readPPM() {
    int rawValue = analogRead(MQ6_ANALOG_PIN);
    // MQ6 curves are logarithmic, but a mapped estimation fits the open variable criteria
    int ppm = map(rawValue, 0, 4095, 200, 10000); 
    return constrain(ppm, 200, 10000);
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("\r\nLast Packet Send Status:\t");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup() {
    Serial.begin(115200);
    pinMode(MQ6_DIGITAL_PIN, INPUT);

    WiFi.mode(WIFI_STA);

    // Init ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(onDataSent);
    
    // Register peer
    memcpy(peerInfo.peer_addr, hubAddress, 6);
    peerInfo.channel = 0;  // System automatically matches hub's AP channel
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        Serial.println("Failed to add peer");
        return;
    }
}

void loop() {
    if ((millis() - lastTime) > SEND_INTERVAL) {
        myData.node_id = NODE_ID;
        myData.ppm = readPPM();
        myData.digital_leak = (digitalRead(MQ6_DIGITAL_PIN) == HIGH);

        // Send message via ESP-NOW
        esp_err_t result = esp_now_send(hubAddress, (uint8_t *) &myData, sizeof(myData));
        
        if (result == ESP_OK) {
            Serial.printf("Sent from Node %d -> PPM: %d, Digital: %d\n", NODE_ID, myData.ppm, myData.digital_leak);
        } else {
            Serial.println("Error sending the data");
        }
        lastTime = millis();
    }
}
