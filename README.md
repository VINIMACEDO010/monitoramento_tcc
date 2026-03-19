# Biodata
Servidor de dados das caldeiras da Burntech
- Servidor de banco de dados (postgres)
- MQTT Broker de recepção de dados em tempo real (mosquitto)
- Subscriber de inserção de dados
  * implementação própria (mosquitto + libpq)
  * servidor modbus recebendo envio de dados dos clientes

## Informações para testes
- IP: 68.183.63.19
- Combinar formato dos dados (ver arquivo `subscriber/biodata.h`):
```c
static char *template[] = {
    "PIT3101",
    "LIT3101",
}
#define PLANT_NAME_MAX_LENGTH 32
typedef struct Payload {
    char plant_name[PLANT_NAME_MAX_LENGTH];
    int64 time;
    int16 data[LENGTH(template)];
} Payload;
```
- Certificado: `ca.crt`
- Chave privada: `publisher.key`

## Docker
O `docker compose` é utilizado para executar e configurar os 3 programas.
O subscriber tem sua `Dockerfile` que diz ao docker como configurar o ambiente
de execução (e criar a imagem). Os outros dois programas partem de imagens
disponíveis no [docker hub](https://hub.docker.com/).

Os programas conseguem se comunicar devido a rede interna criada pelo docker
compose. A configuração fica mais ou menos sincronizada, pois o arquivo
`docker-compose.yaml` contém o que é relevante. Muitas configurações são
implementadas através de variáveis de ambiente.

## Banco de dados
Postgres ([website](https://www.postgresql.org/)).
O arquivo `biodata.sql` faz a configuração inicial do banco, por exemplo:
- Criar tabelas iniciais
- Criar usuários (roles)
- Criar permissões de leitura e escrita (para cada usuário)
Esse arquivo com comandos SQL apenas roda na primeira vez que o docker compose é
iniciado. Nas demais inicializações, o postgres detecta que já existem dados na
pasta associada a um volume docker, e pula a inicialização que, entre outras
coisas, executa os comandos em `biodata.sql`.

O arquivo `postgresql.conf` contém configurações do postgres, por exemplo:
- Como expor o serviço na rede (IP e porta)
- Usar TLS ou não

O arquivo `pg_hba.conf` é a configuração de controle de acesso do postgres,
cada linha especifica um acesso disponível, no formato:
```
<mode>  <db_name>  <db_user>  <host>  <authentication>
```
Por exemplo, a linha abaixo permite que o usuário `username` conecte na database
`database`, a partir do computador cujo host name é `hostname.com`,
autenticando-se através de certificados SSL.
```
hostssl  database  username  hostname.com  cert  clientcert=verify-full
```

Note que o programa `subscriber` usa a biblioteca `libpq` que também é
desenvolvida pela equipe do postgres.

## MQTT Broker
O [mosquitto](https://mosquitto.org/) é configurado com o arquivo
`mosquitto.conf`, por exemplo:
- Como expor o serviço na rede (IP e porta)
- Usar TLS ou não
  * Localização dos certificados e chave

Note que o programa `subscriber` usa a biblioteca `libmosquitto` que também é
desenvolvida pela mesma organização.

## subscriber
Esse programa é desenvolvido em C e tem uma única função:
Receber os dados e inserí-los no banco de dados.
Mais detalhes na pasta `subscriber`

# Executar

```sh
# clonar o repositório (precisa ter acesso)
git clone git@github.com:lucas-mior/biodata.git
# outra opção
git clone https://github.com/lucas-mior/biodata.git

cd biodata

# instalar o docker
sudo apt install docker

# gerar os certificados SSL do servidor
sudo tls/0certificates.sh

# iniciar o docker compose
sudo docker compose up --build
```

## Detalhes de implementação pendentes
Após gerar o certificados SSL, não rodar o commando `tls/0certificates.sh`,
pois isso irá dessincronizar com os publicadores de dados:
Atualmente, é preciso transferir manualmente os certificados para os
publicadores (de maneira isolada ou encriptada).

TODO: implementar um método automatizado de certificação.

## Teste com publisher_mqtt.c
O comando `tls/0certificates.sh` acima gerou, entre outros, os arquivos:
- `ca.crt`
- `publisher.crt`
- `publisher.key`

Copiá-los de maneira segura *do servidor para a sua máquina de teste*.

**Importante:** Arquivos `.key` são as chaves privadas e nunca podem ser compartilhados com ninguém.

Compilar e executar o publicador de teste:
```sh
# instalar dependências
sudo apt update
sudo apt install libmosquitto-dev libpq-dev

# instalar compilador C
sudo apt install gcc 
# outra opção
sudo apt install clang

# clonar o repositório (precisa ter acesso)
git clone git@github.com:lucas-mior/biodata.git
# outra opção
git clone https://github.com/lucas-mior/biodata.git

cd biodata/subscriber

# compilar e executar publicadores
./build.sh publisher_mqtt
./build.sh publisher_modbus
```

## Peça faltante: Monitoramento em tempo real
- ThingsBoard (pesado em memória)
- TagoIO
- TagoCore (open source, bem simples, parece o ideal)
- Grafana (open source, complicado mas poderoso)
- Thinger.io
- webthings.io (mais voltado a aplicações hypadas)

## Escolha de provedor de servidor/serviços web
- AWS (muitas opções, confuso)
- DigitalOcean (free trial de $200 até 2 meses)
- Hetzner (mais em conta)
- Heroku (mais voltado a serviços)
- Vultr
