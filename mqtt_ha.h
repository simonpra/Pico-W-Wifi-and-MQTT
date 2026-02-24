#pragma once
#include <stdint.h>

// Appeler une fois après stdio_init_all()
bool wifi_mqtt_init(const char* ssid, const char* password,
                    const char* mqtt_broker_ip, uint16_t mqtt_port = 1883);

// Publier l'auto-discovery pour tous les capteurs (appeler une fois après connexion)
void mqtt_ha_publish_discovery();

//─── Command Dispatch ──────────────────────────────────────────────
typedef void (*cmd_handler_t)();

struct CmdEntry {
    const char*   name;
    cmd_handler_t handler;
};

//--- Call to register the command handlers functions for incoming MQTT commands
// @param table  : CmdEntry array containing command names and their corresponding handler functions
// @param count  : number of entries in the array
void mqtt_register_commands(const CmdEntry* table, uint8_t count);
//--- SUBSCRIBE to Command Topic for incoming messages (e.g., commands from Home Assistant)
void mqtt_subscribe_commands();

// Publier les valeurs des capteurs (appeler périodiquement)
void mqtt_ha_publish_state(double temperature, double humidity,
                           uint16_t eco2, uint16_t tvoc, uint8_t aqi);

// À appeler dans la boucle principale pour maintenir la connexion
void mqtt_poll();

bool mqtt_is_connected();