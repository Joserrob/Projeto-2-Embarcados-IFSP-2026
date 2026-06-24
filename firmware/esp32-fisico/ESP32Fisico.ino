// ============================================================
// esp32_incendio_mqtt_setpoint.ino
//
// Sistema de Monitoramento de Incêndio com ESP32
//
// FUNCIONALIDADES:
//   - Leitura simulada de temperatura por potenciômetro
//   - Botão físico de emergência
//   - Comandos MQTT individuais e gerais
//   - Setpoint configurável pelo Node-RED
//   - Alarme com retenção até comando de reset
//   - Sprinkler controlado exclusivamente pela temperatura
//   - OLED SSD1306
//
// TÓPICOS DO ESP32-01:
//
// Publicação de estados:
//   ESP32/esp32-01/status
//
// Disponibilidade:
//   ESP32/esp32-01/availability
//
// Comandos individuais:
//   ESP32/esp32-01/command
//
// Comandos gerais:
//   ESP32/all/command
//
// COMANDOS:
//
// Acionar emergência:
//   {"command":"alarm","value":1}
//
// Resetar:
//   {"command":"reset","value":1}
//
// Alterar setpoint:
//   {"command":"setpoint","value":65}
//
// Bibliotecas:
//   - WiFi
//   - PubSubClient
//   - ArduinoJson
//   - Wire
//   - Adafruit GFX Library
//   - Adafruit SSD1306
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
// CONFIGURAÇÃO WI-FI
// ============================================================

const char* WIFI_SSID = "SUA_REDE_WIFI";
const char* WIFI_PASSWORD = "SUA_SENHA_WIFI";

// ============================================================
// CONFIGURAÇÃO MQTT
// ============================================================

const char* MQTT_SERVER = "firewatchifsp.duckdns.org";
const int MQTT_PORT = 1883;

// Dispositivo físico convencional: ESP32 #1.
const char* DEVICE_ID = "esp32-01";

// Tópicos construídos automaticamente.
char TOPIC_STATUS[80];
char TOPIC_AVAILABILITY[80];
char TOPIC_COMMAND_DEVICE[80];

// Todos os ESP32 devem assinar este tópico.
const char* TOPIC_COMMAND_ALL = "ESP32/all/command";

// Cliente de rede e cliente MQTT.
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ============================================================
// PINOS DO CIRCUITO
// ============================================================

#define PIN_POT        34
#define PIN_BTN        15
#define PIN_LED_GREEN  18
#define PIN_LED_ALARM   5
#define PIN_LED_SPR    12

// ============================================================
// OLED SSD1306
// ============================================================

#define OLED_SDA       22
#define OLED_SCL       23
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_ADDR    0x3C

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1
);

bool oledReady = false;

// ============================================================
// CONFIGURAÇÕES DE TEMPERATURA
// ============================================================

// Setpoint utilizado após iniciar ou reiniciar o ESP32.
const float DEFAULT_SETPOINT = 60.0f;

// Limites permitidos pelo sensor simulado.
const float MIN_SETPOINT = 0.0f;
const float MAX_SETPOINT = 100.0f;

// Setpoint configurável por MQTT.
float temperatureSetpoint = DEFAULT_SETPOINT;

// Temperatura atual.
float temperature = 0.0f;

// ============================================================
// INTERVALOS DE TEMPO
// ============================================================

const unsigned long MQTT_PUBLISH_INTERVAL = 1000;
const unsigned long MQTT_RECONNECT_INTERVAL = 3000;
const unsigned long WIFI_RECONNECT_INTERVAL = 5000;
const unsigned long BLINK_INTERVAL = 500;
const unsigned long OLED_INTERVAL = 500;

// ============================================================
// ESTADOS DO SISTEMA
// ============================================================

/*
 * alarmActive:
 *
 * Pode ser acionado por:
 *   - botão físico;
 *   - comando MQTT;
 *   - temperatura acima do setpoint.
 *
 * Permanece ativo até o comando de reset.
 */
bool alarmActive = false;

/*
 * sprinklerActive:
 *
 * Depende exclusivamente da temperatura.
 *
 * Liga:
 *   temperatura > setpoint
 *
 * Desliga:
 *   temperatura <= setpoint
 */
bool sprinklerActive = false;

// Estado utilizado para piscar o LED vermelho e o buzzer.
bool blinkState = false;

// Solicita publicação imediata do status.
bool publishRequested = true;

// Estado anterior do botão.
int lastBtnState = HIGH;

// ============================================================
// CONTROLE DE TEMPO
// ============================================================

unsigned long lastPublish = 0;
unsigned long lastBlink = 0;
unsigned long lastOLED = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastWiFiAttempt = 0;

// ============================================================
// PROTÓTIPOS
// ============================================================

void connectWiFi();
void connectMQTT();
void maintainConnections(unsigned long now);

void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
);

void executeCommand(
    const char* command,
    float value
);

float readTemperature();

void evaluateTemperature();
void updateOutputs();
void updateOLED();

bool publishStatus();

// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);

    // --------------------------------------------------------
    // Configuração dos pinos
    // --------------------------------------------------------

    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_ALARM, OUTPUT);
    pinMode(PIN_LED_SPR, OUTPUT);

    pinMode(PIN_BTN, INPUT_PULLUP);

    // Estado inicial das saídas.
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_ALARM, LOW);
    digitalWrite(PIN_LED_SPR, LOW);

    // --------------------------------------------------------
    // Construção dos tópicos MQTT
    // --------------------------------------------------------

    snprintf(
        TOPIC_STATUS,
        sizeof(TOPIC_STATUS),
        "ESP32/%s/status",
        DEVICE_ID
    );

    snprintf(
        TOPIC_AVAILABILITY,
        sizeof(TOPIC_AVAILABILITY),
        "ESP32/%s/availability",
        DEVICE_ID
    );

    snprintf(
        TOPIC_COMMAND_DEVICE,
        sizeof(TOPIC_COMMAND_DEVICE),
        "ESP32/%s/command",
        DEVICE_ID
    );

    // --------------------------------------------------------
    // Inicialização do OLED
    // --------------------------------------------------------

    Wire.begin(OLED_SDA, OLED_SCL);

    oledReady = display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDR
    );

    if (!oledReady) {
        Serial.println(
            "[OLED] Display não encontrado."
        );
    } else {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);

        display.setCursor(0, 12);
        display.println("Monitor Incendio");

        display.setCursor(0, 28);
        display.print("Dispositivo: ");
        display.println(DEVICE_ID);

        display.setCursor(0, 44);
        display.println("Iniciando...");

        display.display();
    }

    // --------------------------------------------------------
    // Configuração MQTT
    // --------------------------------------------------------

    mqttClient.setServer(
        MQTT_SERVER,
        MQTT_PORT
    );

    mqttClient.setCallback(
        mqttCallback
    );

    mqttClient.setBufferSize(512);

    // --------------------------------------------------------
    // Conexões iniciais
    // --------------------------------------------------------

    connectWiFi();
    connectMQTT();
}

// ============================================================
// LOOP PRINCIPAL
// ============================================================

void loop() {
    unsigned long now = millis();

    // --------------------------------------------------------
    // 1. Mantém Wi-Fi e MQTT conectados
    // --------------------------------------------------------

    maintainConnections(now);

    if (mqttClient.connected()) {
        mqttClient.loop();
    }

    // Guarda os estados anteriores para detectar alterações.
    bool previousAlarmState = alarmActive;
    bool previousSprinklerState = sprinklerActive;

    // --------------------------------------------------------
    // 2. Leitura da temperatura
    // --------------------------------------------------------

    temperature = readTemperature();

    // --------------------------------------------------------
    // 3. Leitura do botão físico
    // --------------------------------------------------------

    int btnState = digitalRead(PIN_BTN);

    if (btnState != lastBtnState) {
        publishRequested = true;
    }

    // Detecta borda de descida.
    if (
        btnState == LOW &&
        lastBtnState == HIGH
    ) {
        Serial.println(
            "[BTN] Botão de emergência pressionado."
        );

        /*
         * O botão ativa somente o alarme.
         * O sprinkler continua dependendo da temperatura.
         */
        alarmActive = true;
    }

    lastBtnState = btnState;

    // --------------------------------------------------------
    // 4. Avaliação da temperatura
    // --------------------------------------------------------

    evaluateTemperature();

    // --------------------------------------------------------
    // 5. Detecta mudanças de estado
    // --------------------------------------------------------

    if (
        alarmActive != previousAlarmState ||
        sprinklerActive != previousSprinklerState
    ) {
        publishRequested = true;
    }

    // --------------------------------------------------------
    // 6. Pisca LED vermelho e buzzer
    // --------------------------------------------------------

    if (
        alarmActive &&
        now - lastBlink >= BLINK_INTERVAL
    ) {
        lastBlink = now;
        blinkState = !blinkState;

        digitalWrite(
            PIN_LED_ALARM,
            blinkState ? HIGH : LOW
        );
    }

    // --------------------------------------------------------
    // 7. Atualização das saídas
    // --------------------------------------------------------

    updateOutputs();

    // --------------------------------------------------------
    // 8. Atualização do OLED
    // --------------------------------------------------------

    if (now - lastOLED >= OLED_INTERVAL) {
        lastOLED = now;
        updateOLED();
    }

    // --------------------------------------------------------
    // 9. Publicação MQTT
    // --------------------------------------------------------

    bool periodicPublish =
        now - lastPublish >= MQTT_PUBLISH_INTERVAL;

    if (
        mqttClient.connected() &&
        (periodicPublish || publishRequested)
    ) {
        if (publishStatus()) {
            lastPublish = now;
            publishRequested = false;
        }
    }
}

// ============================================================
// LEITURA DA TEMPERATURA
// ============================================================

float readTemperature() {
    int raw = analogRead(PIN_POT);

    /*
     * Conversão do ADC:
     *
     * ADC mínimo: 0
     * ADC máximo: 4095
     *
     * Temperatura simulada:
     * 0 °C a 100 °C
     */

    return (
        static_cast<float>(raw) /
        4095.0f
    ) * 100.0f;
}

// ============================================================
// LÓGICA DE TEMPERATURA
// ============================================================

void evaluateTemperature() {
    bool previousSprinklerState =
        sprinklerActive;

    /*
     * O sprinkler depende exclusivamente desta condição:
     *
     * temperatura > setpoint
     */
    sprinklerActive =
        temperature > temperatureSetpoint;

    /*
     * A temperatura elevada também coloca o sistema
     * em estado de alerta.
     */
    if (sprinklerActive) {
        if (!alarmActive) {
            Serial.printf(
                "[TEMP] Temperatura %.1f °C acima "
                "do setpoint %.1f °C.\n",
                temperature,
                temperatureSetpoint
            );
        }

        alarmActive = true;
    }

    // Registra alterações do sprinkler.
    if (
        sprinklerActive != previousSprinklerState
    ) {
        Serial.printf(
            "[SPRINKLER] %s | Temp: %.1f °C | SP: %.1f °C\n",
            sprinklerActive ? "ATIVO" : "INATIVO",
            temperature,
            temperatureSetpoint
        );

        publishRequested = true;
    }
}

// ============================================================
// ATUALIZAÇÃO DAS SAÍDAS
// ============================================================

void updateOutputs() {
    // --------------------------------------------------------
    // LED verde
    // --------------------------------------------------------

    digitalWrite(
        PIN_LED_GREEN,
        alarmActive ? LOW : HIGH
    );

    // --------------------------------------------------------
    // Sprinkler
    // --------------------------------------------------------

    /*
     * Não utilizar alarmActive nesta saída.
     *
     * O sprinkler depende somente de sprinklerActive.
     */
    digitalWrite(
        PIN_LED_SPR,
        sprinklerActive ? HIGH : LOW
    );

    // --------------------------------------------------------
    // LED vermelho e buzzer
    // --------------------------------------------------------

    if (!alarmActive) {
        blinkState = false;

        digitalWrite(
            PIN_LED_ALARM,
            LOW
        );
    }
}

// ============================================================
// ATUALIZAÇÃO DO OLED
// ============================================================

void updateOLED() {
    if (!oledReady) {
        return;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    // Temperatura.
    display.setCursor(0, 0);
    display.print("Temp: ");
    display.print(temperature, 1);
    display.println(" C");

    // Setpoint.
    display.setCursor(0, 12);
    display.print("Setpoint: ");
    display.print(temperatureSetpoint, 1);
    display.println(" C");

    display.drawLine(
        0,
        23,
        SCREEN_WIDTH - 1,
        23,
        SSD1306_WHITE
    );

    // Estado geral.
    display.setCursor(0, 28);
    display.print("Estado: ");

    if (alarmActive) {
        display.println("ALERTA");
    } else {
        display.println("NORMAL");
    }

    // Estado do sprinkler.
    display.setCursor(0, 42);
    display.print("Sprinkler: ");

    if (sprinklerActive) {
        display.println("ATIVO");
    } else {
        display.println("INATIVO");
    }

    // Estado MQTT.
    display.setCursor(0, 54);
    display.print("MQTT: ");

    if (mqttClient.connected()) {
        display.println("ONLINE");
    } else {
        display.println("OFFLINE");
    }

    display.display();
}

// ============================================================
// CONEXÃO WI-FI
// ============================================================

void connectWiFi() {
    lastWiFiAttempt = millis();

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    Serial.print("[WiFi] Conectando");

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    int attempts = 0;

    while (
        WiFi.status() != WL_CONNECTED &&
        attempts < 20
    ) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();

        Serial.print("[WiFi] Conectado. IP: ");
        Serial.println(WiFi.localIP());

        Serial.print("[WiFi] RSSI: ");
        Serial.println(WiFi.RSSI());

        // Indicação visual de conexão.
        if (!alarmActive) {
            for (int i = 0; i < 3; i++) {
                digitalWrite(
                    PIN_LED_GREEN,
                    LOW
                );

                delay(120);

                digitalWrite(
                    PIN_LED_GREEN,
                    HIGH
                );

                delay(120);
            }
        }
    } else {
        Serial.println();

        Serial.println(
            "[WiFi] Falha na conexão. "
            "Sistema operando localmente."
        );
    }
}

// ============================================================
// CONEXÃO MQTT
// ============================================================

void connectMQTT() {
    lastMqttAttempt = millis();

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (mqttClient.connected()) {
        return;
    }

    Serial.print("[MQTT] Conectando ao broker ");
    Serial.print(MQTT_SERVER);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    /*
     * Last Will:
     *
     * Caso o ESP32 perca a conexão abruptamente,
     * o broker publica "offline".
     */
    bool connected = mqttClient.connect(
        DEVICE_ID,
        TOPIC_AVAILABILITY,
        1,
        true,
        "offline"
    );

    if (!connected) {
        Serial.print(
            "[MQTT] Falha. Código: "
        );

        Serial.println(
            mqttClient.state()
        );

        return;
    }

    Serial.println("[MQTT] Conectado.");

    // Publica disponibilidade como mensagem retida.
    mqttClient.publish(
        TOPIC_AVAILABILITY,
        "online",
        true
    );

    // Assina o tópico individual.
    bool subscribedDevice =
        mqttClient.subscribe(
            TOPIC_COMMAND_DEVICE,
            1
        );

    // Assina o tópico geral.
    bool subscribedAll =
        mqttClient.subscribe(
            TOPIC_COMMAND_ALL,
            1
        );

    if (subscribedDevice) {
        Serial.print(
            "[MQTT] Tópico individual: "
        );

        Serial.println(
            TOPIC_COMMAND_DEVICE
        );
    } else {
        Serial.println(
            "[MQTT] Falha ao assinar "
            "o tópico individual."
        );
    }

    if (subscribedAll) {
        Serial.print(
            "[MQTT] Tópico geral: "
        );

        Serial.println(
            TOPIC_COMMAND_ALL
        );
    } else {
        Serial.println(
            "[MQTT] Falha ao assinar "
            "o tópico geral."
        );
    }

    publishRequested = true;
}

// ============================================================
// MANUTENÇÃO DAS CONEXÕES
// ============================================================

void maintainConnections(
    unsigned long now
) {
    // Reconexão Wi-Fi.
    if (WiFi.status() != WL_CONNECTED) {
        if (
            now - lastWiFiAttempt >=
            WIFI_RECONNECT_INTERVAL
        ) {
            Serial.println(
                "[WiFi] Tentando reconectar..."
            );

            connectWiFi();
        }

        return;
    }

    // Reconexão MQTT.
    if (!mqttClient.connected()) {
        if (
            now - lastMqttAttempt >=
            MQTT_RECONNECT_INTERVAL
        ) {
            connectMQTT();
        }
    }
}

// ============================================================
// CALLBACK MQTT
// ============================================================

void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
) {
    Serial.print(
        "[MQTT] Mensagem recebida em "
    );

    Serial.print(topic);
    Serial.print(": ");

    for (
        unsigned int i = 0;
        i < length;
        i++
    ) {
        Serial.print(
            static_cast<char>(payload[i])
        );
    }

    Serial.println();

    // --------------------------------------------------------
    // Validação do tópico
    // --------------------------------------------------------

    bool isDeviceCommand =
        strcmp(
            topic,
            TOPIC_COMMAND_DEVICE
        ) == 0;

    bool isGeneralCommand =
        strcmp(
            topic,
            TOPIC_COMMAND_ALL
        ) == 0;

    if (
        !isDeviceCommand &&
        !isGeneralCommand
    ) {
        Serial.println(
            "[MQTT] Tópico ignorado."
        );

        return;
    }

    // --------------------------------------------------------
    // Interpretação do JSON
    // --------------------------------------------------------

    StaticJsonDocument<256> doc;

    DeserializationError error =
        deserializeJson(
            doc,
            payload,
            length
        );

    if (error) {
        Serial.print(
            "[MQTT] JSON inválido: "
        );

        Serial.println(
            error.c_str()
        );

        return;
    }

    const char* command =
        doc["command"] | "";

    float value =
        doc["value"] | 0.0f;

    executeCommand(
        command,
        value
    );
}

// ============================================================
// EXECUÇÃO DOS COMANDOS MQTT
// ============================================================

void executeCommand(
    const char* command,
    float value
) {
    if (command == nullptr) {
        return;
    }

    String commandString = command;

    commandString.trim();
    commandString.toLowerCase();

    Serial.print("[CMD] Comando: ");
    Serial.print(commandString);

    Serial.print(" | Valor: ");
    Serial.println(value);

    // --------------------------------------------------------
    // COMANDO: ALARM
    // --------------------------------------------------------

    if (
        commandString == "alarm" &&
        value == 1.0f
    ) {
        /*
         * Ativa apenas o alarme.
         *
         * O sprinkler não é ligado diretamente.
         */
        alarmActive = true;

        Serial.println(
            "[CMD] Alarme acionado pelo Node-RED."
        );

        publishRequested = true;

        return;
    }

    // --------------------------------------------------------
    // COMANDO: RESET
    // --------------------------------------------------------

    if (
        commandString == "reset" &&
        value == 1.0f
    ) {
        /*
         * Reavalia primeiro a condição de temperatura.
         */
        sprinklerActive =
            temperature > temperatureSetpoint;

        if (sprinklerActive) {
            /*
             * Não permite operação normal enquanto a
             * temperatura estiver acima do setpoint.
             */
            alarmActive = true;

            Serial.println(
                "[CMD] Reset não executado: "
                "temperatura acima do setpoint."
            );
        } else {
            alarmActive = false;
            blinkState = false;

            digitalWrite(
                PIN_LED_ALARM,
                LOW
            );

            Serial.println(
                "[CMD] Sistema resetado para "
                "operação normal."
            );
        }

        publishRequested = true;

        return;
    }

    // --------------------------------------------------------
    // COMANDO: SETPOINT
    // --------------------------------------------------------

    if (commandString == "setpoint") {
        if (
            value < MIN_SETPOINT ||
            value > MAX_SETPOINT
        ) {
            Serial.printf(
                "[CMD] Setpoint inválido. "
                "Faixa: %.1f a %.1f °C.\n",
                MIN_SETPOINT,
                MAX_SETPOINT
            );

            return;
        }

        temperatureSetpoint = value;

        Serial.printf(
            "[CMD] Novo setpoint: %.1f °C\n",
            temperatureSetpoint
        );

        /*
         * Reavalia imediatamente o sprinkler.
         */
        sprinklerActive =
            temperature > temperatureSetpoint;

        /*
         * Caso a temperatura atual esteja acima do novo
         * setpoint, o sistema também entra em alerta.
         */
        if (sprinklerActive) {
            alarmActive = true;
        }

        Serial.printf(
            "[CMD] Temperatura atual: %.1f °C\n",
            temperature
        );

        Serial.printf(
            "[CMD] Sprinkler: %s\n",
            sprinklerActive
                ? "ATIVO"
                : "INATIVO"
        );

        publishRequested = true;

        return;
    }

    Serial.println(
        "[CMD] Comando ou valor inválido."
    );
}

// ============================================================
// PUBLICAÇÃO DO STATUS MQTT
// ============================================================

bool publishStatus() {
    if (!mqttClient.connected()) {
        return false;
    }

    StaticJsonDocument<512> payload;

    payload["device_id"] =
        DEVICE_ID;

    payload["button"] =
        digitalRead(PIN_BTN) == LOW
            ? 1
            : 0;

    payload["temperature"] =
        temperature;

    // Mantido para compatibilidade.
    payload["threshold"] =
        temperatureSetpoint;

    payload["setpoint"] =
        temperatureSetpoint;

    payload["temperature_above_setpoint"] =
        sprinklerActive ? 1 : 0;

    payload["alarm"] =
        alarmActive ? 1 : 0;

    payload["sprinkler"] =
        sprinklerActive ? 1 : 0;

    payload["led_green"] =
        alarmActive ? 0 : 1;

    payload["led_alarm"] =
        alarmActive && blinkState
            ? 1
            : 0;

    payload["state"] =
        alarmActive
            ? "risco_incendio"
            : "normal";

    payload["wifi_rssi"] =
        WiFi.RSSI();

    payload["uptime_ms"] =
        millis();

    char json[512];

    size_t jsonLength =
        serializeJson(
            payload,
            json,
            sizeof(json)
        );

    if (jsonLength == 0) {
        Serial.println(
            "[MQTT] Erro ao gerar JSON."
        );

        return false;
    }

    /*
     * Retained = true:
     *
     * O dashboard recebe o último estado assim que
     * se conecta ao broker.
     */
    bool published =
        mqttClient.publish(
            TOPIC_STATUS,
            json,
            true
        );

    if (published) {
        Serial.print(
            "[MQTT] Publicado em "
        );

        Serial.print(
            TOPIC_STATUS
        );

        Serial.print(": ");
        Serial.println(json);
    } else {
        Serial.println(
            "[MQTT] Falha na publicação."
        );
    }

    return published;
}