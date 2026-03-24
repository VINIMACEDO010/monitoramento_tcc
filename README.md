# 🔥 Sistema de Monitoramento IoT para Caldeiras (TCC)

Este projeto consiste em uma plataforma escalável para monitoramento de variáveis industriais em tempo real, utilizando arquitetura baseada em microsserviços com Docker, mensageria MQTT e persistência em banco de dados PostgreSQL.

---

## 🏗️ Arquitetura do Sistema

O sistema é composto por três camadas principais:

### 1. 📡 Camada de Aquisição (Publishers)
- Simulação de sensores industriais
- Protocolos utilizados:
  - Modbus TCP
  - MQTT
- Geração de dados:
  - Temperatura da fornalha
  - Pressão da fornalha
  - Vazão da caldeira
  - Pressão do vapor

---

### 2. ⚙️ Camada de Processamento (Subscriber)
- Desenvolvido em linguagem C (alta performance)
- Responsável por:
  - Receber dados via MQTT
  - Processar e tratar os dados
  - Inserir no banco de dados PostgreSQL

---

### 3. 🗄️ Camada de Persistência (PostgreSQL)
- Banco relacional para armazenamento histórico

---

### 🔌 Infraestrutura

Todo o ambiente é orquestrado com Docker:

- PostgreSQL
- Mosquitto (MQTT Broker)
- Subscriber (C)
- Publishers (Modbus + MQTT)
- pgAdmin (interface web)

---

## 🚀 Como Executar o Projeto

### 1. Clonar o repositório

git clone https://github.com/VINIMACEDO010/monitoramento_tcc
cd monitoramento_tcc

---

### 2. Subir o ambiente

docker compose up -d

---

### 3. Verificar containers

docker ps

---

## 🌐 Acessos

### 📊 pgAdmin (interface do banco)

http://localhost:8080

Login:
- Email: admin@admin.com  
- Senha: admin  

---

### 🔌 Conexão com banco

- Host: postgres
- Porta: 5432
- Database: monitoramento
- Usuário: subscriber
- Senha: subscriber

---

## 📈 Consulta de Dados

SELECT * FROM caldeira ORDER BY time DESC LIMIT 20;

---

## 🧠 Conceitos Aplicados

- Arquitetura baseada em eventos (Event-Driven)
- Mensageria com MQTT
- Integração com Modbus TCP
- Processamento em tempo real
- Persistência em banco relacional
- Containerização com Docker
- Sistemas distribuídos

---

## 🚀 Melhorias Futuras

- Dashboard em Grafana
- Sistema de alertas
- API REST
- Interface web

---

## 👨‍💻 Autor

Vinicius Macedo

---

## 📌 Status do Projeto

✅ Sistema funcional  
✅ Dados sendo persistidos  
✅ Integração completa validada  
🚀 Pronto para apresentação  
