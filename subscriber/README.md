# subscriber
Esse programa recebe os dados usando algum [protocolo][#Protocolos] e os insere
no banco de dados postgres. Para isso ele usa as bibliotecas `libmodbus`,
`libmosquitto` e `libpq`.  Os dados recebidos devem estar no formato
adequado e combinado a priori com os publicadores.

# Protocolos
Atualmente são implementados 2 protocols de recepção de dados:
MQTT e Modbus. 
Ver os exemplos `publisher_modbus.c` e `publisher_mqtt.c`.

# Variáveis de ambiente
- `subscriber.c`: ...
- `publisher_mqtt.c`: MQTT_HOST
- `publisher_modbus.c`: MODBUS_HOST

## Formato dos dados atual
Cada payload deve conter:
- `char[32]`: Nome da planta
- `int64`: tempo (segundos)
- `int16[LENGTH(template)]`: array de inteiros de 16 bits com dados
  * Dados inválidos devem ser enviados como zero.
  * Os dados são interpretados na ordem definida na array
    [`template`](#Configuração)


# Configuração
O arquivo `biodata.h` contém algumas configurações importantes:
- array `template`: Ela define qual a ordem dos dados recebídos na array
- Configurações do MQTT: QoS, timeout, entre outros.
- estrutura do payload
