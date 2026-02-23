# MQTT + Home Assistant for Pico W

Library to connect a Raspberry Pi Pico W to an MQTT broker and publish pre-defined sensors data compatible with Home Assistant (auto-discovery).

mqtt_ha.cpp file is heavily commented and intended to be used as a learning resource for C/C++ Wifi and MQTT embeded devices as is the Pico W.

---

## Files

| File | Description |
|---|---|
| `mqtt_ha.h` | Public declarations |
| `mqtt_ha.cpp` | Full implementation |

---

## Overview

```
[Pico W] ──WiFi──▶ [MQTT Broker] ──▶ [Home Assistant]
```

1. WiFi connection via the CYW43 chip (`cyw43_arch`)
   - using lwIP → https://www.nongnu.org/lwip/2_1_x/index.html
2. Connection to the MQTT broker (lwIP `mqtt_client`)
   - https://www.nongnu.org/lwip/2_0_x/group__mqtt.html
3. Publication of an **auto-discovery** message for Home Assistant (HA MQTT Discovery)
   - https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
4. Periodic publication of the **sensor state** as a JSON payload

---

## Public API

### Initialization

```cpp
bool wifi_mqtt_init(
    const char* ssid,           // WiFi SSID
    const char* password,       // WiFi password
    const char* mqtt_broker_ip, // MQTT broker IP address
    uint16_t    mqtt_port       // MQTT broker port
);
```

Connects the Pico W to WiFi, then to the MQTT broker.  
The WiFi connection is blocking (up to 15 s per attempt, 3 retries).  
The MQTT connection waits up to 5 s for the broker to accept.  
Returns `true` if both connections succeed.

---

### Home Assistant Discovery

```cpp
void mqtt_ha_publish_discovery();
```
Publishes the sensor configuration to the HA discovery topic:
`homeassistant/sensor/<DEVICE_ID>/config`

Triggered after successfull connection to WiFI SSID using Async Callback Function.

Then triggers the availability message (`online`) via callback.

### State Publication

```cpp
void mqtt_ha_publish_state( double   temperature,
                            double   humidity,
                            uint16_t eco2,
                            uint16_t tvoc,
                            uint8_t  aqi );
```
(Better Comments will follow)

Publishes a JSON payload to the state topic `pico_env_sensor/state`:

```json
{"temperature":23.5,"humidity":48.2,"eco2":450,"tvoc":120,"aqi":1}
```

Call this periodically in the main loop to update sensor values in Home Assistant.

---

### Main Loop Utilities

```cpp
void mqtt_poll();          // Must be called regularly — drives all async network events
bool mqtt_is_connected();  // Returns true if connected to the broker
```

---

## Internal Callbacks

> Callbacks are invoked by lwIP asynchronously — do not call them directly.

| Callback | Role |
|---|---|
| `mqtt_connection_callback` | Called when the broker connection state changes; triggers discovery on connect |
| `mqtt_publish_request_callback` | Default callback after each publish; logs errors |
| `mqtt_ha_discovery_callback` | Called after discovery publish; publishes `online` to availability topic |
| `mqtt_ha_availability_callback` | Called after `online` publish; sets `discovery_done = true` |

---

## MQTT Topics

| Topic | Description |
|---|---|
| `pico_env_sensor/state` | JSON payload with all sensor values |
| `pico_env_sensor/availability` | `online` / `offline` (also used as Last Will) |
| `homeassistant/sensor/pico_env_sensor/config` | HA auto-discovery config message |

---

## Published Sensors
Personnal and pre-defined sensors. Sensors data are retrieve in the main loop and then passed as arguments to `mqtt_ha_publish_state()`.

| JSON Key | HA Name | Unit | Device Class |
|---|---|---|---|
| `temperature` | Temperature | °C | `temperature` |
| `humidity` | Humidity | % | `humidity` |
| `eco2` | eCO2 | ppm | `carbon_dioxide` |
| `tvoc` | TVOC | ppb | `volatile_organic_compounds_parts` |
| `aqi` | AQI | — | `aqi` |

---

## Notes

- **Last Will & Testament (LWT):** if the Pico disconnects unexpectedly, the broker automatically publishes `offline` to `pico_env_sensor/availability`, letting Home Assistant mark the device as unavailable.
- **Async networking:** all MQTT operations are asynchronous. `mqtt_poll()` (which calls `cyw43_arch_poll()`) must be called regularly in the main loop to process network events and invoke callbacks.
- **QoS:** QoS 1 is used for discovery and LWT messages (broker acknowledgment required). State messages use QoS 1 as well.
- **Discovery:** the discovery payload is sent only once per connection. It is re-sent automatically if the broker connection drops and reconnects.
