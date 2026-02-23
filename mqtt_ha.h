#pragma once
#include <stdint.h>

// Appeler une fois après stdio_init_all()
bool wifi_mqtt_init(const char* ssid, const char* password,
                    const char* mqtt_broker_ip, uint16_t mqtt_port = 1883);

// Publier l'auto-discovery pour tous les capteurs (appeler une fois après connexion)
void mqtt_ha_publish_discovery();

// Publier les valeurs des capteurs (appeler périodiquement)
void mqtt_ha_publish_state(double temperature, double humidity,
                           uint16_t eco2, uint16_t tvoc, uint8_t aqi);

// À appeler dans la boucle principale pour maintenir la connexion
void mqtt_poll();

bool mqtt_is_connected();