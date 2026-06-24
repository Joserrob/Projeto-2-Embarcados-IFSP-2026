-- ============================================================
-- FIREWATCH IFSP CATANDUVA
-- Banco de dados utilizado pelo fluxo Node-RED
-- ============================================================

CREATE DATABASE IF NOT EXISTS iot_incendio
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE iot_incendio;

CREATE TABLE IF NOT EXISTS leituras_esp32 (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,

    device_id VARCHAR(50) NOT NULL,
    mqtt_topic VARCHAR(150) NOT NULL,

    temperatura DECIMAL(6,2) NOT NULL,
    setpoint DECIMAL(6,2) NULL,
    acima_setpoint TINYINT(1) NOT NULL DEFAULT 0,

    botao TINYINT(1) NOT NULL DEFAULT 0,
    alarme TINYINT(1) NOT NULL DEFAULT 0,
    sprinkler TINYINT(1) NOT NULL DEFAULT 0,

    led_verde TINYINT(1) NOT NULL DEFAULT 0,
    led_alarme TINYINT(1) NOT NULL DEFAULT 0,

    estado VARCHAR(40) NOT NULL,

    wifi_rssi SMALLINT NULL,
    uptime_ms BIGINT UNSIGNED NULL,

    recebido_em TIMESTAMP(3)
        NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3),

    PRIMARY KEY (id),

    INDEX idx_device_time (
        device_id,
        recebido_em
    ),

    INDEX idx_time (
        recebido_em
    ),

    INDEX idx_alarm_sprinkler_time (
        alarme,
        sprinkler,
        recebido_em
    )
);
