# 🔥 Sistema de Monitoramento IoT para Caldeiras (TCC)

Plataforma para monitoramento de variáveis operacionais de caldeiras industriais em ambiente
simulado, construída com arquitetura de microsserviços, mensageria MQTT, protocolo Modbus TCP,
persistência em PostgreSQL e visualização em Grafana. Todo o ambiente sobe com **um único
comando** e já vem com dashboard e banco pré-configurados.

---

## 🏗️ Arquitetura do Sistema

O sistema é organizado em três camadas principais:

### 1. 📡 Camada de Aquisição (Publishers)
- Simulação de sensores industriais escritos em linguagem C
- Protocolos: **MQTT** e **Modbus TCP**
- Variáveis geradas:
  - Temperatura da fornalha (1200–1400 °C)
  - Pressão da fornalha (−7 a 0 mmca)
  - Vazão da caldeira (10–12 t/h)
  - Pressão do vapor (10–12 bar)

### 2. ⚙️ Camada de Processamento (Subscriber)
- Desenvolvido em linguagem C (alta performance)
- Recebe dados via MQTT e Modbus TCP
- Trata as mensagens e persiste no PostgreSQL (criação dinâmica de tabelas/colunas)

### 3. 🗄️ Camada de Persistência (PostgreSQL)
- Banco relacional para o histórico operacional
- Dois usuários por segurança: `subscriber` (escrita) e `readonly` (leitura, usado pelo Grafana)

---

## 🔌 Infraestrutura

Todo o ambiente é orquestrado com Docker Compose:

| Serviço | Função |
|---------|--------|
| PostgreSQL 16 | Banco de dados |
| Mosquitto 2.0 | Broker MQTT |
| Subscriber (C) | Processamento e persistência |
| Publisher MQTT (C) | Simulação de sensor via MQTT |
| Publisher Modbus (C) | Simulação de sensor via Modbus TCP |
| pgAdmin | Interface web do banco |
| Grafana | Dashboards (fonte de dados e painel **provisionados automaticamente**) |

---

## 🚀 Como Executar

### 1. Clonar o repositório

```bash
git clone https://github.com/VINIMACEDO010/monitoramento_tcc
cd monitoramento_tcc
```

### 2. Subir o ambiente

```bash
docker compose up -d --build
```

Na primeira execução o Docker compila os programas em C e baixa as imagens — pode levar alguns
minutos. Depois disso, **tudo sobe automaticamente**: banco, broker, geração de dados ao vivo,
e o Grafana já com a fonte de dados conectada e o dashboard carregado. Nenhum passo manual.

### 3. Verificar os containers

```bash
docker ps
```

---

## 🌐 Acessos

### 📈 Grafana (dashboard)
- URL: http://localhost:3000
- Login: `admin` / `admin`
- O dashboard **"Monitoramento de Caldeira — TCC"** já vem carregado, com valor atual e
  histórico de cada variável.

### 📊 pgAdmin (banco)
- URL: http://localhost:8080
- Login: `admin@admin.com` / `admin`

### 🔌 Conexão direta com o banco
- Host: `postgres` · Porta: `5432`
- Database: `monitoramento`
- Usuário: `subscriber` / Senha: `subscriber`

### Consulta rápida
```sql
SELECT * FROM caldeira ORDER BY time DESC LIMIT 20;
```

---

## 💻 Compatibilidade

As imagens dos serviços em C usam base `linux/amd64`, garantindo funcionamento em:
- Windows e Linux (Intel/AMD) — nativo
- macOS com Apple Silicon (M1/M2/M3) — via emulação do Docker Desktop

---

## 🧠 Conceitos Aplicados

- Arquitetura orientada a eventos (Event-Driven)
- Mensageria com MQTT e integração com Modbus TCP
- Processamento em tempo aproximado
- Persistência em banco relacional (PostgreSQL) com separação de privilégios
- Containerização e orquestração com Docker Compose
- Provisionamento como código (Infra as Code) do Grafana
- Sistemas distribuídos e observabilidade

---

## 🔭 Melhorias Futuras

- Sistema de alertas por faixa de operação
- API REST
- Autenticação e TLS no broker MQTT
- Monitoramento simultâneo de múltiplas plantas

---

## 👨‍💻 Autor

Vinicius Macedo

---

## 📌 Status do Projeto

✅ Sistema funcional
✅ Dados persistidos
✅ Dashboard automático no Grafana
✅ Ambiente reprodutível com um único comando
