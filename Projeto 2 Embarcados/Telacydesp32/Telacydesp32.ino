// ============================================================
// ESP32 CYD — Sistema de Monitoramento de Incêndio
// Interface Touch com Slider de Temperatura
//
// Dispositivo MQTT: esp32-03
//
// Interface gráfica preservada do projeto FireWatch.
//
// Tópicos MQTT:
//
// Publicação:
//   ESP32/esp32-03/status
//   ESP32/esp32-03/availability
//
// Recepção:
//   ESP32/esp32-03/command
//   ESP32/all/command
//
// Comandos aceitos:
//
// Emergência:
//   {"command":"alarm","value":1}
//
// Operação normal:
//   {"command":"reset","value":1}
//
// Alteração do setpoint:
//   {"command":"setpoint","value":65}
//
// Bibliotecas:
//   - WiFi
//   - PubSubClient
//   - ArduinoJson
//   - SPI
//   - TFT_eSPI
//   - XPT2046_Touchscreen
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ============================================================
// CONFIGURAÇÃO DE REDE
// ============================================================

const char* WIFI_SSID     = "Jose";
const char* WIFI_PASSWORD = "brasil2021";

// ============================================================
// CONFIGURAÇÃO MQTT
// ============================================================

const char* MQTT_SERVER = "18.229.134.103";
const int MQTT_PORT = 1883;

const char* DEVICE_ID = "esp32-03";

// Tópicos construídos automaticamente
char TOPIC_STATUS[80];
char TOPIC_AVAILABILITY[80];
char TOPIC_COMMAND_DEVICE[80];

// Tópico geral recebido pelos três dispositivos
const char* TOPIC_COMMAND_ALL = "ESP32/all/command";

// Cliente TCP e cliente MQTT
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ============================================================
// CONFIGURAÇÃO DO DISPLAY
// ============================================================

#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240

TFT_eSPI tft = TFT_eSPI();

// ============================================================
// PALETA DE CORES — BASEADA NO SITE FIREWATCH
//
// Mantida sem alterações.
// ============================================================

#define RGB565(r, g, b) \
    ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define INV_RGB565(r, g, b) \
    RGB565(255-(r), 255-(g), 255-(b))

#define UI_BG        INV_RGB565(244, 241, 234)
#define UI_CARD      INV_RGB565(255, 255, 255)
#define UI_HEADER    INV_RGB565(40, 40, 40)
#define UI_TEXT_DARK INV_RGB565(51, 51, 51)
#define UI_TEXT_MID  INV_RGB565(136, 136, 136)
#define UI_TEXT_LITE INV_RGB565(255, 255, 255)
#define UI_RED       INV_RGB565(231, 76, 60)
#define UI_ORANGE    INV_RGB565(230, 126, 34)
#define UI_GREEN     INV_RGB565(46, 204, 113)
#define UI_BLUE      INV_RGB565(52, 152, 219)
#define UI_BORDER    INV_RGB565(220, 220, 220)

// ============================================================
// CONFIGURAÇÃO DO TOUCH — ESP32 CYD
// ============================================================

#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33

SPIClass touchSPI = SPIClass(VSPI);

XPT2046_Touchscreen touch(
    XPT2046_CS,
    XPT2046_IRQ
);

// ============================================================
// LED RGB EMBARCADO DO ESP32 CYD
// ============================================================

#define LED_RED    4
#define LED_GREEN 16
#define LED_BLUE  17

// ============================================================
// BOTÃO FÍSICO OPCIONAL
// ============================================================

#define PIN_BOOT_BUTTON 0

// ============================================================
// PARÂMETROS DO SISTEMA
// ============================================================

// Setpoint utilizado ao inicializar o ESP32
const float DEFAULT_SETPOINT = 60.0f;

// Limites permitidos pelo slider e pelo dashboard
const float MIN_SETPOINT = 0.0f;
const float MAX_SETPOINT = 100.0f;

// Setpoint atual
float temperatureSetpoint = DEFAULT_SETPOINT;

// Intervalos
const unsigned long MQTT_PUBLISH_INTERVAL = 2000;
const unsigned long MQTT_RECONNECT_INTERVAL = 3000;
const unsigned long WIFI_RECONNECT_INTERVAL = 5000;
const unsigned long UI_INTERVAL = 150;

// ============================================================
// COORDENADAS DA INTERFACE
// ============================================================

// Slider de temperatura
const int SLIDER_X = 30;
const int SLIDER_Y = 84;
const int SLIDER_W = 260;
const int SLIDER_H = 18;

// Botão de emergência
const int BTN_EMG_X = 20;
const int BTN_EMG_Y = 178;
const int BTN_EMG_W = 280;
const int BTN_EMG_H = 45;

// ============================================================
// VARIÁVEIS DE ESTADO
// ============================================================

// Temperatura simulada pelo slider
float temperature = 25.0f;

/*
 * Estado geral de alerta.
 *
 * Pode ser ativado por:
 * - botão touch;
 * - botão físico;
 * - comando MQTT;
 * - temperatura acima do setpoint.
 *
 * Permanece ativo até receber o comando reset.
 */
bool alarmActive = false;

/*
 * Estado do sprinkler.
 *
 * Depende exclusivamente da temperatura:
 *
 * temperatura > setpoint  → ligado
 * temperatura <= setpoint → desligado
 */
bool sprinklerActive = false;

// Mantido para compatibilidade com a interface original
bool blinkState = false;

// Solicita publicação MQTT imediata
bool publishRequested = true;

// Estado anterior do botão físico
bool lastBootButtonState = HIGH;

// ============================================================
// CONTROLE DE TEMPO
// ============================================================

unsigned long lastPublish = 0;
unsigned long lastUI = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastWiFiAttempt = 0;

// ============================================================
// CONTROLE DE REDESENHO DA INTERFACE
// ============================================================

float lastDrawnTemperature = -100.0f;
float lastDrawnSetpoint = -100.0f;

bool lastDrawnAlarm = false;
bool lastDrawnSprinkler = false;
bool lastDrawnWiFiConnected = false;

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

void activateAlarm(const char* source);
void resetAlarmByMQTT();

void updateSystemLogic();
void updatePhysicalLEDs();

void drawInterface();
void drawHeader();
void drawTemperatureBox();
void drawSlider();
void drawIndicators();
void drawSystemState();
void drawButtons();
void drawWiFiStatus();

void handleTouch();

bool getTouchPoint(
    int &x,
    int &y
);

bool pointInside(
    int x,
    int y,
    int bx,
    int by,
    int bw,
    int bh
);

bool publishStatus();

// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);

    // --------------------------------------------------------
    // Configuração dos LEDs físicos
    // --------------------------------------------------------

    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);

    pinMode(
        PIN_BOOT_BUTTON,
        INPUT_PULLUP
    );

    /*
     * Estado inicial mantido conforme o projeto original.
     * Os LEDs do CYD geralmente trabalham com lógica invertida.
     */
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_BLUE, HIGH);

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
    // Inicialização do display
    // --------------------------------------------------------

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(UI_BG);

    // --------------------------------------------------------
    // Inicialização do touchscreen
    // --------------------------------------------------------

    touchSPI.begin(
        XPT2046_CLK,
        XPT2046_MISO,
        XPT2046_MOSI,
        XPT2046_CS
    );

    touch.begin(touchSPI);
    touch.setRotation(1);

    drawInterface();

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

    // Atualiza o indicador de conexão
    drawInterface();
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

    // --------------------------------------------------------
    // 2. Processa touchscreen
    // --------------------------------------------------------

    handleTouch();

    // --------------------------------------------------------
    // 3. Processa botão físico BOOT
    // --------------------------------------------------------

    bool bootButtonState =
        digitalRead(PIN_BOOT_BUTTON);

    if (
        bootButtonState == LOW &&
        lastBootButtonState == HIGH
    ) {
        /*
         * O botão físico ativa somente o alarme.
         * O sprinkler permanece dependente da temperatura.
         */
        activateAlarm(
            "BOTAO FISICO BOOT"
        );
    }

    lastBootButtonState =
        bootButtonState;

    // --------------------------------------------------------
    // 4. Atualiza a lógica de controle
    // --------------------------------------------------------

    updateSystemLogic();

    // --------------------------------------------------------
    // 5. Atualiza os LEDs físicos
    // --------------------------------------------------------

    updatePhysicalLEDs();

    // --------------------------------------------------------
    // 6. Atualiza a interface gráfica
    // --------------------------------------------------------

    if (now - lastUI >= UI_INTERVAL) {
        lastUI = now;

        bool needRedraw = false;

        bool wifiConnected =
            WiFi.status() == WL_CONNECTED;

        if (
            fabsf(
                temperature -
                lastDrawnTemperature
            ) >= 0.2f
        ) {
            needRedraw = true;
        }

        if (
            fabsf(
                temperatureSetpoint -
                lastDrawnSetpoint
            ) >= 0.1f
        ) {
            needRedraw = true;
        }

        if (
            alarmActive !=
            lastDrawnAlarm
        ) {
            needRedraw = true;
        }

        if (
            sprinklerActive !=
            lastDrawnSprinkler
        ) {
            needRedraw = true;
        }

        if (
            wifiConnected !=
            lastDrawnWiFiConnected
        ) {
            needRedraw = true;
        }

        if (needRedraw) {
            drawInterface();

            lastDrawnTemperature =
                temperature;

            lastDrawnSetpoint =
                temperatureSetpoint;

            lastDrawnAlarm =
                alarmActive;

            lastDrawnSprinkler =
                sprinklerActive;

            lastDrawnWiFiConnected =
                wifiConnected;
        }
    }

    // --------------------------------------------------------
    // 7. Publicação MQTT
    // --------------------------------------------------------

    bool periodicPublish =
        now - lastPublish >=
        MQTT_PUBLISH_INTERVAL;

    if (
        mqttClient.connected() &&
        (
            periodicPublish ||
            publishRequested
        )
    ) {
        if (publishStatus()) {
            lastPublish = now;
            publishRequested = false;
        }
    }
}

// ============================================================
// LÓGICA PRINCIPAL DO SISTEMA
// ============================================================

void updateSystemLogic() {
    bool previousSprinklerState =
        sprinklerActive;

    /*
     * O sprinkler depende exclusivamente desta condição.
     */
    sprinklerActive =
        temperature >
        temperatureSetpoint;

    /*
     * Temperatura acima do setpoint também ativa
     * o estado geral de alerta.
     */
    if (
        sprinklerActive &&
        !alarmActive
    ) {
        activateAlarm(
            "TEMPERATURA ACIMA DO SETPOINT"
        );
    }

    // Detecta alteração no sprinkler
    if (
        sprinklerActive !=
        previousSprinklerState
    ) {
        Serial.printf(
            "[SPRINKLER] %s | Temp: %.1f C | SP: %.1f C\n",
            sprinklerActive
                ? "ATIVO"
                : "INATIVO",
            temperature,
            temperatureSetpoint
        );

        publishRequested = true;
    }
}

// ============================================================
// ATIVAÇÃO DO ALARME
// ============================================================

void activateAlarm(
    const char* source
) {
    /*
     * Não altera sprinklerActive.
     *
     * O sprinkler deve continuar dependendo somente
     * da comparação temperatura > setpoint.
     */
    alarmActive = true;
    blinkState = true;

    publishRequested = true;

    Serial.print(
        "[ALARME] Ativado por: "
    );

    Serial.println(source);
}

// ============================================================
// RESET PELO NODE-RED
// ============================================================

void resetAlarmByMQTT() {
    /*
     * Reavalia a temperatura antes de permitir
     * o retorno ao estado normal.
     */
    sprinklerActive =
        temperature >
        temperatureSetpoint;

    if (sprinklerActive) {
        /*
         * Se a temperatura ainda estiver acima do
         * setpoint, o alarme permanece ativo.
         */
        alarmActive = true;

        Serial.println(
            "[MQTT] Reset nao executado: "
            "temperatura acima do setpoint."
        );
    } else {
        alarmActive = false;
        blinkState = false;

        Serial.println(
            "[MQTT] Sistema retornou "
            "ao estado NORMAL."
        );
    }

    publishRequested = true;
}

// ============================================================
// LEDS FÍSICOS
// ============================================================

void updatePhysicalLEDs() {
    /*
     * Mantida a mesma lógica visual do projeto original.
     */
    if (alarmActive) {
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(LED_RED, LOW);
        digitalWrite(LED_BLUE, LOW);
    } else {
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, HIGH);
        digitalWrite(LED_BLUE, HIGH);
    }
}

// ============================================================
// INTERFACE GRÁFICA
// ============================================================

void drawInterface() {
    tft.fillScreen(UI_BG);

    drawHeader();
    drawTemperatureBox();
    drawSlider();
    drawIndicators();
    drawButtons();
    drawWiFiStatus();
}

// ============================================================
// CABEÇALHO
// ============================================================

void drawHeader() {
    uint16_t headerColor =
        alarmActive
            ? UI_RED
            : UI_HEADER;

    tft.fillRect(
        0,
        0,
        SCREEN_WIDTH,
        32,
        headerColor
    );

    tft.setTextColor(
        alarmActive
            ? UI_TEXT_LITE
            : UI_ORANGE
    );

    tft.setTextSize(2);
    tft.setCursor(10, 8);
    tft.print("O");

    tft.setTextColor(
        UI_TEXT_LITE
    );

    tft.setCursor(28, 8);
    tft.print("FireWatch");
}

// ============================================================
// TEMPERATURA
// ============================================================

void drawTemperatureBox() {
    tft.fillRoundRect(
        10,
        40,
        300,
        75,
        8,
        UI_CARD
    );

    tft.drawRoundRect(
        10,
        40,
        300,
        75,
        8,
        UI_BORDER
    );

    tft.setTextSize(2);
    tft.setTextColor(UI_TEXT_DARK);
    tft.setCursor(25, 52);
    tft.print("Temperatura");

    tft.setTextSize(3);

    tft.setTextColor(
        alarmActive
            ? UI_RED
            : UI_ORANGE
    );

    if (temperature >= 100.0f) {
        tft.setCursor(185, 48);
    } else if (temperature >= 10.0f) {
        tft.setCursor(205, 48);
    } else {
        tft.setCursor(225, 48);
    }

    tft.print(temperature, 1);

    tft.setTextSize(2);
    tft.print(" C");
}

// ============================================================
// SLIDER DE TEMPERATURA
// ============================================================

void drawSlider() {
    uint16_t stateColor =
        alarmActive
            ? UI_RED
            : UI_ORANGE;

    // Fundo do trilho
    tft.fillRoundRect(
        SLIDER_X,
        SLIDER_Y,
        SLIDER_W,
        SLIDER_H,
        8,
        UI_BORDER
    );

    float ratio =
        temperature / 100.0f;

    ratio = constrain(
        ratio,
        0.0f,
        1.0f
    );

    int filledWidth =
        ratio * SLIDER_W;

    // Preenchimento ativo
    if (filledWidth > 6) {
        tft.fillRoundRect(
            SLIDER_X,
            SLIDER_Y,
            filledWidth,
            SLIDER_H,
            8,
            stateColor
        );
    }

    /*
     * Linha de setpoint.
     *
     * Mantém o mesmo estilo visual da linha crítica
     * original, porém acompanha o setpoint recebido
     * pelo Node-RED.
     */
    int thresholdX =
        SLIDER_X +
        (
            temperatureSetpoint /
            100.0f
        ) *
        SLIDER_W;

    tft.drawLine(
        thresholdX,
        SLIDER_Y - 4,
        thresholdX,
        SLIDER_Y + SLIDER_H + 4,
        UI_RED
    );

    // Botão do slider
    int knobX =
        SLIDER_X +
        filledWidth;

    knobX = constrain(
        knobX,
        SLIDER_X + 6,
        SLIDER_X + SLIDER_W - 6
    );

    tft.fillCircle(
        knobX,
        SLIDER_Y + SLIDER_H / 2,
        9,
        UI_CARD
    );

    tft.drawCircle(
        knobX,
        SLIDER_Y + SLIDER_H / 2,
        9,
        UI_BORDER
    );

    tft.fillCircle(
        knobX,
        SLIDER_Y + SLIDER_H / 2,
        4,
        stateColor
    );
}

// ============================================================
// INDICADORES DE ESTADO
// ============================================================

void drawIndicators() {
    tft.fillRoundRect(
        10,
        122,
        300,
        45,
        8,
        UI_CARD
    );

    tft.drawRoundRect(
        10,
        122,
        300,
        45,
        8,
        UI_BORDER
    );

    int yCircle = 144;
    int yText = 140;

    tft.setTextSize(1);
    tft.setTextColor(UI_TEXT_DARK);

    // Estado geral
    uint16_t colorAlarme =
        alarmActive
            ? UI_RED
            : UI_BORDER;

    tft.fillCircle(
        25,
        yCircle,
        6,
        colorAlarme
    );

    tft.setCursor(
        38,
        yText
    );

    tft.print(
        alarmActive
            ? "EM ALARME"
            : "Normal"
    );

    // Estado dos sprinklers
    uint16_t colorSprink =
        sprinklerActive
            ? UI_BLUE
            : UI_BORDER;

    tft.fillCircle(
        135,
        yCircle,
        6,
        colorSprink
    );

    tft.setCursor(
        148,
        yText
    );

    tft.print(
        sprinklerActive
            ? "Sprinklers ON"
            : "Sprinklers OFF"
    );
}

// ============================================================
// FUNÇÃO MANTIDA POR COMPATIBILIDADE
// ============================================================

void drawSystemState() {
    // As informações permanecem integradas em drawIndicators().
}

// ============================================================
// BOTÃO DE EMERGÊNCIA
// ============================================================

void drawButtons() {
    uint16_t btnColor =
        alarmActive
            ? UI_RED
            : UI_CARD;

    uint16_t txtColor =
        alarmActive
            ? UI_TEXT_LITE
            : UI_RED;

    uint16_t borderColor =
        UI_RED;

    tft.fillRoundRect(
        BTN_EMG_X,
        BTN_EMG_Y,
        BTN_EMG_W,
        BTN_EMG_H,
        8,
        btnColor
    );

    tft.drawRoundRect(
        BTN_EMG_X,
        BTN_EMG_Y,
        BTN_EMG_W,
        BTN_EMG_H,
        8,
        borderColor
    );

    tft.setTextSize(2);
    tft.setTextColor(txtColor);

    if (alarmActive) {
        tft.setCursor(
            BTN_EMG_X + 40,
            BTN_EMG_Y + 15
        );

        tft.print(
            "EMERGENCIA ATIVA"
        );
    } else {
        tft.setCursor(
            BTN_EMG_X + 50,
            BTN_EMG_Y + 15
        );

        tft.print(
            "FORCAR ALARME"
        );
    }
}

// ============================================================
// STATUS DO WIFI
// ============================================================

void drawWiFiStatus() {
    tft.setTextSize(1);

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {
        tft.setTextColor(
            UI_GREEN
        );

        tft.setCursor(
            255,
            12
        );

        tft.print("WiFi OK");
    } else {
        tft.setTextColor(
            UI_TEXT_MID
        );

        tft.setCursor(
            250,
            12
        );

        tft.print("Offline");
    }
}

// ============================================================
// TOUCHSCREEN
// ============================================================

void handleTouch() {
    int x;
    int y;

    if (!getTouchPoint(x, y)) {
        return;
    }

    // --------------------------------------------------------
    // Slider de temperatura
    // --------------------------------------------------------

    if (
        x >= SLIDER_X &&
        x <= SLIDER_X + SLIDER_W &&
        y >= SLIDER_Y - 18 &&
        y <= SLIDER_Y + SLIDER_H + 18
    ) {
        float ratio =
            static_cast<float>(
                x - SLIDER_X
            ) /
            static_cast<float>(
                SLIDER_W
            );

        if (ratio < 0.0f) {
            ratio = 0.0f;
        }

        if (ratio > 1.0f) {
            ratio = 1.0f;
        }

        temperature =
            ratio * 100.0f;

        Serial.print(
            "[SLIDER] Temperatura ajustada: "
        );

        Serial.println(
            temperature,
            1
        );

        publishRequested = true;

        delay(80);
        return;
    }

    // --------------------------------------------------------
    // Botão touch de emergência
    // --------------------------------------------------------

    if (
        pointInside(
            x,
            y,
            BTN_EMG_X,
            BTN_EMG_Y,
            BTN_EMG_W,
            BTN_EMG_H
        )
    ) {
        /*
         * O toque ativa somente o alarme.
         * O sprinkler depende da temperatura.
         */
        activateAlarm(
            "BOTAO TOUCH EMERGENCIA"
        );

        delay(250);
        return;
    }
}

// ============================================================
// LEITURA DO TOUCH
// ============================================================

bool getTouchPoint(
    int &x,
    int &y
) {
    if (!touch.touched()) {
        return false;
    }

    TS_Point p =
        touch.getPoint();

    x = map(
        p.x,
        200,
        3700,
        0,
        SCREEN_WIDTH
    );

    y = map(
        p.y,
        240,
        3800,
        0,
        SCREEN_HEIGHT
    );

    x = constrain(
        x,
        0,
        SCREEN_WIDTH - 1
    );

    y = constrain(
        y,
        0,
        SCREEN_HEIGHT - 1
    );

    return true;
}

// ============================================================
// VERIFICA SE O TOQUE ESTÁ DENTRO DE UMA ÁREA
// ============================================================

bool pointInside(
    int x,
    int y,
    int bx,
    int by,
    int bw,
    int bh
) {
    return (
        x >= bx &&
        x <= bx + bw &&
        y >= by &&
        y <= by + bh
    );
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
            static_cast<char>(
                payload[i]
            )
        );
    }

    Serial.println();

    // --------------------------------------------------------
    // Verifica o tópico recebido
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
            "[MQTT] Topico ignorado."
        );

        return;
    }

    // --------------------------------------------------------
    // Interpreta o JSON recebido
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
            "[MQTT] JSON invalido: "
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

    String commandString =
        command;

    commandString.trim();
    commandString.toLowerCase();

    Serial.print(
        "[CMD] Comando: "
    );

    Serial.print(
        commandString
    );

    Serial.print(
        " | Valor: "
    );

    Serial.println(value);

    // --------------------------------------------------------
    // COMANDO DE EMERGÊNCIA
    // --------------------------------------------------------

    if (
        commandString == "alarm" &&
        value == 1.0f
    ) {
        activateAlarm(
            "NODE-RED"
        );

        return;
    }

    // --------------------------------------------------------
    // COMANDO DE RESET
    // --------------------------------------------------------

    if (
        (
            commandString == "reset" ||
            commandString == "normal"
        ) &&
        value == 1.0f
    ) {
        resetAlarmByMQTT();

        return;
    }

    /*
     * Compatibilidade com sistemas anteriores:
     * alarm = 0 equivale a reset.
     */
    if (
        commandString == "alarm" &&
        value == 0.0f
    ) {
        resetAlarmByMQTT();

        return;
    }

    // --------------------------------------------------------
    // COMANDO DE SETPOINT
    // --------------------------------------------------------

    if (
        commandString ==
        "setpoint"
    ) {
        if (
            value < MIN_SETPOINT ||
            value > MAX_SETPOINT
        ) {
            Serial.printf(
                "[CMD] Setpoint invalido. "
                "Faixa permitida: %.1f a %.1f C.\n",
                MIN_SETPOINT,
                MAX_SETPOINT
            );

            return;
        }

        temperatureSetpoint =
            value;

        Serial.printf(
            "[CMD] Novo setpoint: %.1f C\n",
            temperatureSetpoint
        );

        /*
         * Reavalia imediatamente o estado
         * do sprinkler.
         */
        updateSystemLogic();

        publishRequested = true;

        return;
    }

    Serial.println(
        "[CMD] Comando ou valor invalido."
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

    payload["temperature"] =
        temperature;

    /*
     * Mantido para compatibilidade com o dashboard
     * desenvolvido anteriormente.
     */
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

    payload["button"] =
        digitalRead(
            PIN_BOOT_BUTTON
        ) == LOW
            ? 1
            : 0;

    payload["led"] =
        alarmActive ? 1 : 0;

    payload["normal"] =
        alarmActive ? 0 : 1;

    payload["led_green"] =
        alarmActive ? 0 : 1;

    payload["led_alarm"] =
        alarmActive ? 1 : 0;

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
     * o Node-RED recebe o estado mais recente
     * imediatamente após se conectar.
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
            "[MQTT] Falha na publicacao."
        );
    }

    return published;
}

// ============================================================
// CONEXÃO MQTT
// ============================================================

void connectMQTT() {
    lastMqttAttempt =
        millis();

    if (
        WiFi.status() !=
        WL_CONNECTED
    ) {
        return;
    }

    if (mqttClient.connected()) {
        return;
    }

    Serial.print(
        "[MQTT] Conectando ao broker "
    );

    Serial.print(MQTT_SERVER);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    /*
     * Last Will:
     *
     * Caso o ESP32 perca a conexão inesperadamente,
     * o broker publicará "offline".
     */
    bool connected =
        mqttClient.connect(
            DEVICE_ID,
            TOPIC_AVAILABILITY,
            1,
            true,
            "offline"
        );

    if (!connected) {
        Serial.print(
            "[MQTT] Falha. Codigo: "
        );

        Serial.println(
            mqttClient.state()
        );

        return;
    }

    Serial.println(
        "[MQTT] Conectado."
    );

    // Disponibilidade retida
    mqttClient.publish(
        TOPIC_AVAILABILITY,
        "online",
        true
    );

    // Comando individual
    bool subscribedDevice =
        mqttClient.subscribe(
            TOPIC_COMMAND_DEVICE,
            1
        );

    // Comando geral
    bool subscribedAll =
        mqttClient.subscribe(
            TOPIC_COMMAND_ALL,
            1
        );

    if (subscribedDevice) {
        Serial.print(
            "[MQTT] Topico individual: "
        );

        Serial.println(
            TOPIC_COMMAND_DEVICE
        );
    } else {
        Serial.println(
            "[MQTT] Falha ao assinar "
            "topico individual."
        );
    }

    if (subscribedAll) {
        Serial.print(
            "[MQTT] Topico geral: "
        );

        Serial.println(
            TOPIC_COMMAND_ALL
        );
    } else {
        Serial.println(
            "[MQTT] Falha ao assinar "
            "topico geral."
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
    if (
        WiFi.status() !=
        WL_CONNECTED
    ) {
        if (
            now - lastWiFiAttempt >=
            WIFI_RECONNECT_INTERVAL
        ) {
            Serial.println(
                "[WiFi] Desconectado. "
                "Tentando reconectar..."
            );

            connectWiFi();
        }

        return;
    }

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
// CONEXÃO WIFI
// ============================================================

void connectWiFi() {
    lastWiFiAttempt =
        millis();

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {
        return;
    }

    Serial.print(
        "[WiFi] Conectando"
    );

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    int attempts = 0;

    while (
        WiFi.status() !=
        WL_CONNECTED &&
        attempts < 20
    ) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {
        Serial.print(
            "\n[WiFi] Conectado! IP: "
        );

        Serial.println(
            WiFi.localIP()
        );

        Serial.print(
            "[WiFi] RSSI: "
        );

        Serial.println(
            WiFi.RSSI()
        );
    } else {
        Serial.println(
            "\n[WiFi] Falha — "
            "operando offline"
        );
    }
}