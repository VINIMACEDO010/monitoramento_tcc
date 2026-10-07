# 🔥 Sistema de Monitoramento IoT para Caldeiras (TCC)

Protótipo de monitoramento das variáveis operacionais de uma caldeira industrial em ambiente
simulado, desenvolvido como Trabalho de Conclusão de Curso de Sistemas de Informação (UNIDAVI, 2026).
Usa mensageria MQTT, protocolo Modbus TCP, persistência em PostgreSQL e visualização em Grafana.
Todo o ambiente sobe com **um único comando** e já vem com banco e painel configurados.

---

## 🏗️ Arquitetura do Sistema

O sistema é organizado em quatro camadas:

### 1. 📡 Aquisição (publicadores)
- Sensores simulados escritos em linguagem C
- Uma leitura a cada **5 segundos** (`SAMPLE_INTERVAL_SECONDS` em `subscriber/biodata.h`)
- Dois publicadores com o mesmo formato de mensagem (estrutura `Payload`):
  - **MQTT**, representando sensores IoT (tópico `caldeira/dados`, QoS 1)
  - **Modbus TCP**, representando a leitura a partir de um CLP
- Variáveis geradas:
  - Temperatura da fornalha (1200 a 1400 °C)
  - Pressão da fornalha (−7 a 0 mmca)
  - Vazão da caldeira (10 a 12 t/h)
  - Pressão do vapor (10 a 12 bar)

### 2. ⚙️ Processamento (subscriber)
- Programa em C que recebe MQTT e Modbus TCP (porta 1502) no mesmo processo, com `poll()`
- Valida o nome da planta, cria tabelas e colunas conforme os dados chegam
- Inserção idempotente (`ON CONFLICT (time) DO NOTHING`), sem leituras duplicadas
- Abandona os privilégios de root ao iniciar (usuário `subscriber`, UID 2000)
- Reconecta sozinho ao broker e ao banco depois de uma queda

### 3. 🗄️ Persistência (PostgreSQL)
- Uma tabela por planta, com o instante da leitura (`time`) como chave primária
- Dois usuários: `subscriber` (escrita) e `readonly` (somente leitura, usado pelo Grafana)
- Senhas com `scram-sha-256`

### 4. 📈 Visualização (Grafana)
- Valor atual e histórico de cada variável, atualizados a cada 5 segundos
- Mostradores em **verde** dentro da faixa e em **vermelho** fora dela
- Limites da faixa como linhas tracejadas nos gráficos

---

## 🔌 Infraestrutura

Todo o ambiente é orquestrado com Docker Compose:

| Serviço | Função | Porta |
|---------|--------|-------|
| postgres | Banco de dados (PostgreSQL 16) | 5432 |
| mosquitto | Broker MQTT (Mosquitto 2.0) | 1883 |
| subscriber | Processamento e persistência | 1502 |
| publisher-mqtt | Sensor simulado via MQTT | |
| publisher-modbus | Sensor simulado via Modbus TCP (perfil `modbus`) | |
| pgadmin | Interface web do banco | 8080 |
| grafana | Painéis (fonte de dados e painel provisionados automaticamente) | 3000 |

Como a simulação representa **uma única caldeira**, só um publicador fica ativo por vez.
Por padrão sobe o MQTT. O Modbus fica no perfil `modbus` e só sobe quando pedido.

---

## 🚀 Como Executar

### 1. Clonar o repositório

```bash
git clone https://github.com/VINIMACEDO010/monitoramento_tcc
cd monitoramento_tcc
```

### 2. Subir o ambiente (publicador MQTT)

```bash
docker compose up -d --build
```

Na primeira execução o Docker compila os programas em C e baixa as imagens, o que pode levar
alguns minutos. Depois disso tudo sobe automaticamente: banco, broker, geração de dados e o
Grafana já com a fonte de dados e o painel carregados.

### 3. Trocar para o publicador Modbus

```bash
docker compose stop publisher-mqtt
docker compose --profile modbus up -d --build publisher-modbus
```

### 4. Pausar e retomar

```bash
docker compose --profile modbus stop   # pausa todos os serviços
docker compose up -d                   # retoma
```

> ⚠️ `docker compose down -v` apaga os volumes, ou seja, **todo o histórico gravado**.

---

## 🌐 Acessos

### 📈 Grafana
- URL: http://localhost:3000
- Login: `admin` / `admin`
- O painel **"Monitoramento de Caldeira — TCC"** já vem carregado.

### 📊 pgAdmin
- URL: http://localhost:8080
- Login: `admin@admin.com` / `admin`

### 🔌 Banco de dados
- Host: `postgres` · Porta: `5432` · Database: `monitoramento`
- Usuário: `subscriber` / Senha: `subscriber`

```bash
docker compose exec postgres psql -U subscriber -d monitoramento -c "SELECT * FROM caldeira ORDER BY time DESC LIMIT 20;"
```

---

## 🧪 Ferramenta de teste (`teste_envio`)

Envia ao broker uma leitura montada à mão, no mesmo formato dos publicadores. Serve para
produzir situações que o funcionamento normal não gera:

```bash
# temperatura acima da faixa (mostrador fica vermelho)
docker compose exec -e BIODATA_HOST=mosquitto subscriber /app/bin/teste_envio -T 1450

# a mesma leitura enviada 3 vezes (o banco guarda só uma)
docker compose exec -e BIODATA_HOST=mosquitto subscriber /app/bin/teste_envio -t 1000000000 -n 3

# planta nova (a tabela é criada sozinha)
docker compose exec -e BIODATA_HOST=mosquitto subscriber /app/bin/teste_envio -p caldeira_teste
```

Opções: `-p` planta, `-t` instante (segundos Unix), `-n` número de envios,
`-T` `-F` `-V` `-S` valores de temperatura, pressão da fornalha, vazão e pressão do vapor.

---

## 💻 Compatibilidade

Testado no Windows com Docker Desktop. As imagens dos programas em C usam base `linux/amd64`
e devem funcionar também em Linux e macOS, mas esses sistemas não foram testados.

---

## 🧠 Conceitos Aplicados

- Arquitetura orientada a eventos
- Mensageria com MQTT e integração com Modbus TCP
- Persistência em banco relacional com separação de privilégios
- Containerização e orquestração com Docker Compose
- Provisionamento do Grafana como código
- Observabilidade e rastreabilidade do dado

---

## 🔭 Trabalhos Futuros

- Ligar o protótipo ao CLP de uma caldeira real pelo Modbus TCP
- Guardar as leituras no publicador enquanto o broker ou o banco estiverem fora
- Alertas por mensagem para o operador e o engenheiro
- Cadastros de equipamentos e de eventos (Registro de Segurança da NR-13)
- Testes em Linux e macOS
- Previsão de falhas com aprendizado profundo sobre o histórico

---

## 👨‍💻 Autor

Vinicius Policarpo Macedo, Sistemas de Informação, UNIDAVI

---

## 📌 Status do Projeto

✅ Protótipo funcional e validado por testes funcionais (setembro e outubro de 2026)
✅ Coleta a cada 5 segundos por MQTT ou Modbus TCP
✅ Histórico contínuo e rastreável no PostgreSQL
✅ Painel no Grafana com sinalização de desvio
✅ Ambiente reprodutível com um único comando
