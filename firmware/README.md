# Firmware dos dispositivos

| Pasta | Dispositivo | Identificador MQTT |
|---|---|---|
| `esp32-fisico` | ESP32 físico com OLED | `esp32-01` |
| `esp32-wokwi` | ESP32 simulado no Wokwi | `esp32-02` |
| `esp32-cyd` | ESP32 CYD com TFT touch | `esp32-03` |

Antes de gravar os dispositivos físicos, altere:

```cpp
const char* WIFI_SSID = "SUA_REDE_WIFI";
const char* WIFI_PASSWORD = "SUA_SENHA_WIFI";
```

O broker está configurado para:

```cpp
const char* MQTT_SERVER = "firewatchifsp.duckdns.org";
const int MQTT_PORT = 1883;
```

O projeto Wokwi utiliza `Wokwi-GUEST`, sem senha.
