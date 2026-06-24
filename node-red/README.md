# Fluxo Node-RED

O arquivo `flows.json` contém o fluxo completo do FireWatch:

- recepção de telemetria MQTT;
- armazenamento das leituras no MySQL;
- dashboard de monitoramento;
- comandos individuais e gerais;
- alteração remota de setpoint;
- controle de disponibilidade online/offline;
- histórico operacional das últimas 6 horas;
- página de análises com filtros de dispositivo, data e hora;
- gráfico histórico de temperatura e setpoint;
- indicadores e tabela de registros.

## Nós adicionais

No diretório de usuário do Node-RED:

```bash
cd ~/.node-red
npm install @flowfuse/node-red-dashboard node-red-node-mysql
```

Após instalar, reinicie o Node-RED e importe `flows.json`.

As credenciais do MySQL não são armazenadas no arquivo de fluxo. Configure-as no nó
`MySQL AWS Local` após a importação.
