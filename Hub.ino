#include <WiFi.h>
#include <esp_now.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// ================= CONFIGURATION =================
#define BUZZER_PIN 25
const int LED_PINS[4] = {26, 27, 14, 12}; 
#define RESET_BUTTON_PIN 13

int SET_PPM_THRESHOLD = 2000; 

// MQTT Settings
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic_pub = "lpg/system/status";
const char* mqtt_topic_sub = "lpg/system/cmd"; // For remote reset
// =================================================

WiFiClient espClient;
PubSubClient client(espClient);

struct NodeData {
    int ppm = 200;
    bool digital_leak = false;
    bool warning_triggered = false;
    bool critical_leak_triggered = false;
};
NodeData nodes[4];

typedef struct struct_message {
    int node_id;
    int ppm;
    bool digital_leak;
} struct_message;
struct_message incomingData;

bool system_latched_alarm = false;
unsigned long last_buzzer_toggle = 0;
unsigned long last_led_toggle = 0;
bool buzzer_state = false;
bool led_flash_state = false;
unsigned long last_mqtt_publish = 0;

void resetAlarms() {
    system_latched_alarm = false;
    for(int i=0; i<4; i++) {
        nodes[i].critical_leak_triggered = false;
        nodes[i].warning_triggered = false;
        digitalWrite(LED_PINS[i], LOW);
    }
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("System Alarms Reset.");
    publishSystemState();
}

// Publish payload as a lightweight JSON string to EMQX
void publishSystemState() {
    String payload = "{";
    payload += "\"latched\":" + String(system_latched_alarm ? "true" : "false") + ",";
    payload += "\"nodes\":[";
    for(int i=0; i<4; i++) {
        payload += "{";
        payload += "\"id\":" + String(i+1) + ",";
        payload += "\"ppm\":" + String(nodes[i].ppm) + ",";
        payload += "\"crit\":" + String(nodes[i].critical_leak_triggered ? "true" : "false") + ",";
        payload += "\"warn\":" + String(nodes[i].warning_triggered ? "true" : "false");
        payload += "}";
        if(i < 3) payload += ",";
    }
    payload += "]}";
    
    client.publish(mqtt_topic_pub, payload.c_str());
}

// Handle incoming commands from the Website (Remote Reset)
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    if (String(topic) == mqtt_topic_sub && message == "RESET") {
        resetAlarms();
    }
}

void reconnectMQTT() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "LPGHubClient-" + String(random(0, 1000));
        if (client.connect(clientId.c_str())) {
            Serial.println("connected");
            client.subscribe(mqtt_topic_sub);
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void onDataRecv(const uint8_t * mac, const uint8_t *incomingDataBytes, int len) {
    memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
    int idx = incomingData.node_id - 1;
    
    if (idx >= 0 && idx < 4) {
        nodes[idx].ppm = incomingData.ppm;
        nodes[idx].digital_leak = incomingData.digital_leak;

        int warnThreshold = SET_PPM_THRESHOLD * 0.70;

        if (nodes[idx].digital_leak || nodes[idx].ppm >= SET_PPM_THRESHOLD) {
            nodes[idx].critical_leak_triggered = true;
            system_latched_alarm = true; 
        } 
        else if (nodes[idx].ppm >= warnThreshold) {
            nodes[idx].warning_triggered = true;
        } 
        else if (!system_latched_alarm) {
            nodes[idx].warning_triggered = false;
        }
        
        // Instant update on MQTT when data rolls in
        publishSystemState();
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    for(int i=0; i<4; i++) pinMode(LED_PINS[i], OUTPUT);

    WiFiManager wm;
    wm.autoConnect("LPG_Hub_Setup_AP");

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);
}

void loop() {
    if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop();

    unsigned long currentMillis = millis();

    // Hardware Reset Button
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
        delay(50);
        if(digitalRead(RESET_BUTTON_PIN) == LOW) {
            resetAlarms();
        }
    }

    bool anyCritical = false;
    bool anyWarning = false;
    for (int i = 0; i < 4; i++) {
        if (nodes[i].critical_leak_triggered) anyCritical = true;
        if (nodes[i].warning_triggered) anyWarning = true;
    }

    // Alarm Logic 
    if (anyCritical) {
        if (currentMillis - last_buzzer_toggle >= 100) {
            buzzer_state = !buzzer_state;
            digitalWrite(BUZZER_PIN, buzzer_state);
            last_buzzer_toggle = currentMillis;
        }
        for (int i = 0; i < 4; i++) {
            if (nodes[i].critical_leak_triggered) digitalWrite(LED_PINS[i], HIGH);
        }
    } 
    else if (anyWarning) {
        if (currentMillis - last_led_toggle >= 1000) {
            led_flash_state = !led_flash_state;
            last_led_toggle = currentMillis;
        }
        if (currentMillis - last_buzzer_toggle >= 2000) {
            buzzer_state = !buzzer_state;
            digitalWrite(BUZZER_PIN, buzzer_state);
            last_buzzer_toggle = currentMillis;
        }
        for (int i = 0; i < 4; i++) {
            if (nodes[i].warning_triggered) digitalWrite(LED_PINS[i], led_flash_state);
            else digitalWrite(LED_PINS[i], LOW);
        }
    } 
    else {
        digitalWrite(BUZZER_PIN, LOW);
        for(int i=0; i<4; i++) digitalWrite(LED_PINS[i], LOW);
    }
    
    // Heartbeat publish every 10 seconds if nothing changes
    if (currentMillis - last_mqtt_publish >= 10000) {
        publishSystemState();
        last_mqtt_publish = currentMillis;
    }
}
