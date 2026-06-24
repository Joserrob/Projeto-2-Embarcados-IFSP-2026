-- Consultas auxiliares do projeto FireWatch

USE iot_incendio;

-- Último registro de cada dispositivo
SELECT l.*
FROM leituras_esp32 AS l
INNER JOIN (
    SELECT
        device_id,
        MAX(recebido_em) AS ultima_leitura
    FROM leituras_esp32
    GROUP BY device_id
) AS u
    ON u.device_id = l.device_id
   AND u.ultima_leitura = l.recebido_em
ORDER BY l.device_id;

-- Histórico de temperatura de um dispositivo
SELECT
    device_id,
    temperatura,
    setpoint,
    estado,
    sprinkler,
    recebido_em
FROM leituras_esp32
WHERE device_id = 'esp32-01'
  AND recebido_em BETWEEN
      '2026-06-24 00:00:00'
      AND
      '2026-06-24 23:59:59'
ORDER BY recebido_em ASC;

-- Registros com alerta ou sprinkler ativo
SELECT
    device_id,
    temperatura,
    setpoint,
    alarme,
    sprinkler,
    recebido_em
FROM leituras_esp32
WHERE alarme = 1
   OR sprinkler = 1
ORDER BY recebido_em DESC;
