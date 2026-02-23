#include "mqtt_ha.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"

// ── Configuration ──────────────────────────────────────────
#define DEVICE_ID        "pico_env_sensor"
#define DEVICE_NAME      "Pico Env Sensor"
#define STATE_TOPIC      "pico_env_sensor/state"
#define DISCOVERY_PREFIX "homeassistant"

static mqtt_client_t* mqtt_client = nullptr;
static ip_addr_t broker_addr;
static bool connected = false;
static bool discovery_done = false;
static uint16_t broker_port_g = 1883;

//───────────────────────────────────────────────────────────────────
//─── MQTT Callbacks ────────────────────────────────────────────────
//───────────────────────────────────────────────────────────────────
// Callbacks are used by lwIP (cyw43_arch) when requests have been sent
// to the broker and a response is received, as Wifi requests are asynchronous...
// or when the connection status changes.
static void mqtt_connection_callback(
    mqtt_client_t *client,              // current MQTT client (is a pointer)
    void *arg,                          // user supplied argument (is a pointer, set to nullptr in connect call) 
    mqtt_connection_status_t status     // connection status
) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("WIFI: connected\n");
        connected = true;
        discovery_done = false;  // Trigger discovery in the main loop
        printf("MQTT: Connecting to broker\n");
        // Publish discovery immediately upon connection
        mqtt_ha_publish_discovery();
    } else {
        printf("MQTT: Connection failed, status=%d\n", status);
        connected = false;
    }
}

// Callback invoked by lwIP when an MQTT publish request completes.
// This is particularly useful for QoS 1 or 2 to confirm that the broker acknowledged the message.
static void mqtt_publish_request_callback(
    void *arg,      // User-supplied argument passed to mqtt_publish (currently nullptr)
    err_t result    // Result of the publish request (ERR_OK if successful)
) {
    if (result != ERR_OK) {
        printf("MQTT: Publish error (%d)\n", result);
    }
}

//───────────────────────────────────────────────────────────────────
//─── MQTT Publish Helper ───────────────────────────────────────────
//───────────────────────────────────────────────────────────────────
// Publish a message (payload) to a specified MQTT topic via the brocker.
// The brocker will then forward the message to any subscribed clients, like Home Assistant.
// The retain flag indicates if the broker should keep the last message for new subscribers.
static bool mqtt_publish_msg(const char* topic, const char* payload, bool retain, mqtt_request_cb_t cb = nullptr, void* arg = nullptr) {
    if (!connected || !mqtt_client) return false;
    
    // Simple Default Callback: if no callback is provided.
    mqtt_request_cb_t final_cb = (cb != nullptr) ? cb : mqtt_publish_request_callback;

    err_t err = mqtt_publish(
        mqtt_client,        // current MQTT client (is a pointer)
        topic,              // topic – Publish topic string
        payload,            // payload – Data to publish (NULL is allowed)
        strlen(payload),    // payload_length – Length of payload (0 is allowed)
        1,                  // qos – Quality of service, 0 1 or 2
        retain ? 1 : 0,     // retain – MQTT retain flag
        final_cb,           // cb – Callback to call when publish is complete or has timed out
        arg                 // arg – User supplied argument to publish callback
    );

    if (err != ERR_OK) {
        printf("MQTT: Publish error %d on %s\n", err, topic);
        return false;
    }
    return true;
}

//───────────────────────────────────────────────────────────────────
//─── Flush and Wait (lwIP processing) ──────────────────────────────
//───────────────────────────────────────────────────────────────────
static void mqtt_flush_and_wait(uint32_t ms) {
    for (uint32_t i = 0; i < ms / 10; i++) {
        cyw43_arch_poll();
        sleep_ms(10);
    }
}

//───────────────────────────────────────────────────────────────────
//─── WiFi Connection ───────────────────────────────────────────────
//───────────────────────────────────────────────────────────────────
static bool wifi_connect(const char* ssid, const char* password) {
    // Initialize CYW43 architecture (WiFi chip)
    if (cyw43_arch_init()) {
        printf("WiFi: Error in cyw43_arch_init!\n");
        return false;
    }
    
    // Enable Station mode (WiFi client)
    cyw43_arch_enable_sta_mode();

    printf("WiFi: connecting to %s...\n", ssid);
    int retries = 3;
    
    // WiFi connection blocks the thread and can take a long time (10s+)
    while (retries-- > 0) {
        // Attempt to connect with a 15-second timeout
        if (cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 15000) == 0) {
            char ip_str[16];
            // Get the IP from netif (cyw43 network interface -> netif_default) in binary
            // and convert it to string (ipaddr_ntoa_r) for user readable output.
            ipaddr_ntoa_r(netif_ip4_addr(netif_default), ip_str, sizeof(ip_str));
            printf("WiFi: Connected! IP=%s\n", ip_str);
            return true;
        }
        printf("WiFi: Connection attempt failed, retry...\n");
    }
    
    // If we are here, all attempts failed !
    printf("WiFi: Unable to connect after multiple attempts\n");
    return false;
}

//───────────────────────────────────────────────────────────────────
//─── WiFi + MQTT Init ──────────────────────────────────────────────
//───────────────────────────────────────────────────────────────────
bool wifi_mqtt_init(
    const char* ssid,               // Wifi SSID (pointer to string)
    const char* password,           // Wifi password (pointer to string)
    const char* mqtt_broker_ip,     // MQTT broker IP (pointer to string)
    uint16_t mqtt_port              // MQTT broker port
) {
    broker_port_g = mqtt_port;

    // Connect to WiFi
    if (!wifi_connect(ssid, password)) {
        return false;
    }

    // Resolve broker IP
    // Convert broker IP string to binary format (ipaddr_aton) and store in broker_addr
    ipaddr_aton(mqtt_broker_ip, &broker_addr);

    // Create MQTT client
    mqtt_client = mqtt_client_new();
    if (!mqtt_client) {
        printf("MQTT: Client allocation failed\n");
        return false;
    }

    //--- https://www.nongnu.org/lwip/2_1_x/structmqtt__connect__client__info__t.html
    struct mqtt_connect_client_info_t ci = {};
    ci.client_id   = DEVICE_ID;
    ci.client_user = NULL;
    ci.client_pass = NULL;
    ci.keep_alive  = 60;        // in seconds, 0 to disable
    // ci.tls_config  = NULL;      // TLS configuration, NULL for no TLS
    //--- will stand for "Last Will and Testament"
    //--- message (will_msg) published (on will_topic) by the broker if the client is disconnected unexpectedly
    ci.will_topic   = DEVICE_ID "/availability";
    ci.will_msg     = "offline";
    //--- QOS (Quality of Service) set the "acknowledgment" level for the will message.
    //--- qos 0: at most once   // juste send, no ack
    //--- qos 1: at least once  // send, wait for ack, retry if no ack... can lead to duplicates
    //--- qos 2: exactly once   // send, ensure delivery exactly once
    ci.will_qos     = 1;
    //--- retain flag (will_retain) indicate if the broker should retain the will message.
    ci.will_retain  = 1;

    err_t err = mqtt_client_connect(
        mqtt_client,            // client – MQTT client (is a pointer)
        &broker_addr,           // ipaddr – Server IP   (as a pointer -> &)
        broker_port_g,          // port – Server port
        mqtt_connection_callback,     // cb – Connection state change callback
        nullptr,                // arg – User supplied argument to connection callback
        &ci                     // client_info – Client identification and connection options  (as a pointer -> &)
    );
    
    if (err != ERR_OK) {
        printf("MQTT: Connect error (%d)\n", err);
        return false;
    }

    // Wait for connection (connected defined in callback)
    // by blocking with for loop and POLLING lwIP (cyw43_arch_poll)
    // meaning letting cyw43_arch do whatever it has to...
    // TODO: can be made into Async with management in main loop.
    for (int i = 0; i < 500 && !connected; i++) {
        cyw43_arch_poll();
        sleep_ms(10);
    }

    return connected;
}

//───────────────────────────────────────────────────────────────────
//─── CALLBACKS for MQTT discovery ──────────────────────────────────
//───────────────────────────────────────────────────────────────────
void mqtt_ha_availability_callback(void *arg, err_t result) {
    if (result == ERR_OK) {
        printf("MQTT: Availability message published successfully\n");
        printf("Discovery is now ONLINE ^^\n");
        discovery_done = true;
    } else {
        printf("MQTT: Failed to publish availability message (%d)\n", result);
    }
}

void mqtt_ha_discovery_callback(void *arg, err_t result) {
    if (result == ERR_OK) {
        printf("MQTT: Discovery message published successfully\n");
        printf("Sending availability message to confirm discovery\n");
        mqtt_publish_msg(DEVICE_ID "/availability", "online", true, mqtt_ha_availability_callback);
    } else {
        printf("MQTT: Failed to publish discovery message (%d)\n", result);
    }
}

//───────────────────────────────────────────────────────────────────
//─── Auto-Discovery Home Assistant ─────────────────────────────────
//───────────────────────────────────────────────────────────────────
void mqtt_ha_publish_discovery() {
    if (!connected || discovery_done) return;

    const char* device_block =
        "\"dev\":{"                                     // DEVICE info block, used by Home Assistant to display device info and group entities
            "\"ids\":[\"" DEVICE_ID "\"],"                  // Unique ID for the device, used by Home Assistant to identify it (can be used to link entities)
            "\"name\":\"" DEVICE_NAME "\","                 // Name of the device, used by Home Assistant to display it
            "\"mf\":\"DIY\","                               // Manufacturer, used by Home Assistant to display it
            "\"mdl\":\"Pico W + ENS160 + AHT2x\","          // Model, used by Home Assistant to display it
            "\"sw\":\"1.1\","                               // Software version, used by Home Assistant to display it
            "\"hw\":\"rev0.85\""                            // Hardware version, used by Home Assistant to display it
        "},"
        "\"avty_t\":\"" DEVICE_ID "/availability\"";    // Availability topic, used by Home Assistant to know if the device is online or offline
                                                        // Availability topic need to be the same as the WILL topic defined in mqtt_connect_client_info_t

    //--- Struct to define the sensors[] array.
    struct SensorConfig {
        const char* type;
        const char* name;
        const char* uid_suffix;
        const char* val_tpl;
        const char* unit;
        const char* dev_cla;
    };

    const SensorConfig sensors[] = {
        // type         // name         // uid_suffix   // val_tpl      // unit     // dev_cla
        {"temperature", "Température",  "_temp",        "temperature",  "°C",       "temperature"                       },
        {"humidity",    "Humidité",     "_hum",         "humidity",     "%",        "humidity"                          },
        {"eco2",        "eCO2",         "_eco2",        "eco2",         "ppm",      "carbon_dioxide"                    },
        {"tvoc",        "TVOC",         "_tvoc",        "tvoc",         "ppb",      "volatile_organic_compounds_parts"  },
        {"aqi",         "AQI",          "_aqi",         "aqi",          "",         "aqi"                               },
    };

    // cmps_block is used to build the "cmps" block of the discovery payload, which contains the list of components (sensors) to be published.
    // offset and max_len are used to "append" to cmps_block without overflowing it,
    // which is done by keeping track of the current length (offset) and the maximum allowed length (max_len).
    // 1024 bytes/char is "just enough" to hold the discovery payload for 5 sensors, but need to be increased if more sensors are added.
    char cmps_block[1024];
    size_t offset = 0;
    size_t max_len = sizeof(cmps_block);

    //--- Start the "cmps" block with "cmps:{" string, and update the offset accordingly.
    offset += snprintf(cmps_block + offset, max_len - offset, "\"cmps\":{");

    //--- Keep track for the last sensor
    int i = 0;
    const int nb_sensors = sizeof(sensors) / sizeof(sensors[0]);
    char uid[64];

    //--- Build the payload for each sensor
    for (const auto& s : sensors) {
        
        //--- Unique Component ID for this sensor
        snprintf(uid, sizeof(uid), "sensor_component_%s", s.uid_suffix);

        //--- Unit_of_measurement is only added if the unit is not empty, to avoid HomeAssistant errors.
        char unit_block[64] = "";
        if (strlen(s.unit) > 0) {
            snprintf(unit_block, sizeof(unit_block), "\"unit_of_meas\":\"%s\",", s.unit);
        }

        //--- Build the Composant payload for this sensor
        offset += snprintf(
            cmps_block + offset,    // append to current offset in cmps_block
            max_len - offset,       // check for remaining space in cmps_block
            "\"sensor%s\":{"                            // component unique Key (sensor_temp, ...)
                "\"p\":\"sensor\","                     // platform (type of component, here sensor)
                "\"dev_cla\":\"%s\","                   // device class (used by Home Assistant to display the right icon and unit, and for some automations)
                "%s"                                    // unit_of_measurement, only added if unit is not empty
                "\"val_tpl\":\"{{ value_json.%s }}\","  // value template, used by Home Assistant to extract the value from the JSON payload published on the state topic
                "\"uniq_id\":\"" DEVICE_ID "%s\""       // unique ID for this component, used by Home Assistant to identify it (can be used to link entities)
            "}",
            s.uid_suffix, s.dev_cla, unit_block, s.val_tpl, s.uid_suffix
        );
        
        // add a comma only if it's not the last sensor, to avoid JSON syntax error in the "cmps" block.
        if (i < nb_sensors - 1) {
            offset += snprintf(cmps_block + offset, max_len - offset, ",");
        }
    }

    // CLOSING the "cmps" block with "}" string.
    offset += snprintf(cmps_block + offset, max_len - offset, "}");

    //--- TOPIC and PAYLOAD for the discovery message
    char topic[128];
    char payload[1152];
    // https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
    // The discovery topic needs to follow a specific format:
    //      homeassistant/device/<DEVICE_ID>/config
    snprintf(topic, sizeof(topic),
                DISCOVERY_PREFIX "/sensor/" DEVICE_ID "/config");

    //--- FINAL payload (device_block + stat_t global + cmps_block)
    snprintf(payload, sizeof(payload),
        "{"
            "%s,"                               // "dev": {...} block with "avty_t" (availability topic)
            "\"stat_t\":\"" STATE_TOPIC "\","   // State topic for Home Assistant to subscribe to. Same topic for all sensors of the device.
            "%s"                                // "cmps": {...} block with all sensors of the device
        "}", 
        device_block, cmps_block);
    
    printf("MQTT: Publishing discovery message to topic %s\n", topic);
    printf("MQTT: Discovery payload:\n%s\n", payload);

    //--- PUBLISH MQTT discovery message only once for all sensors.
    //--- Callback will confirm if the message was published successfully,
    //--- and then trigger the availability message to confirm discovery.
    mqtt_publish_msg(topic, payload, true, mqtt_ha_discovery_callback);
}

//───────────────────────────────────────────────────────────────────
//─── STATE PUBLICATION ─────────────────────────────────────────────
//───────────────────────────────────────────────────────────────────
// Comments and Doc will follow....
void mqtt_ha_publish_state(double temperature, double humidity,
                           uint16_t eco2, uint16_t tvoc, uint8_t aqi) {
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"temperature\":%.1f,"
         "\"humidity\":%.1f,"
         "\"eco2\":%u,"
         "\"tvoc\":%u,"
         "\"aqi\":%u}",
        temperature, humidity, eco2, tvoc, aqi);

    mqtt_publish_msg(STATE_TOPIC, payload, false);
    printf("MQTT: State publié: %s\n", payload);
}

void mqtt_poll() {
    if( !connected ) return;
    //--- let cyw43_arch do its thing (handle WiFi and MQTT events, call callbacks, etc.)
    //--- need to be called regularly in the main loop to maintain the connection and process events.
    cyw43_arch_poll();
}

bool mqtt_is_connected() {
    return connected;
}