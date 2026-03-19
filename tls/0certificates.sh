#!/bin/sh

set -e
dir="$PWD"
cd "$(dirname "$(realpath "$0")")"
rm -f -- *.key *.csr *.crt *.srl

# 1. Generate a CA private key
openssl genrsa -out ca.key 2048

# 2. Create a self-signed CA certificate
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt -subj "/CN=MyTestCA"

# 3. Generate mosquitto private key
openssl genrsa -out mosquitto.key 2048

# 5. Create mosquitto CSR with SAN
openssl req -new -key mosquitto.key -out mosquitto.csr -config mosquitto.cnf

# 6. Sign mosquitto certificate with CA and include SAN
openssl x509 -req -in mosquitto.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out mosquitto.crt -days 3650 -extensions v3_req -extfile mosquitto.cnf

# 7. Generate subscriber key and cert (mutual auth)
openssl genrsa -out subscriber.key 2048
openssl req -new -key subscriber.key -out subscriber.csr -subj "/CN=subscriber"
openssl x509 -req -in subscriber.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out subscriber.crt -days 3650

# 8. Generate second subscriber key and cert
openssl genrsa -out publisher.key 2048
openssl req -new -key publisher.key -out publisher.csr -subj "/CN=publisher"
openssl x509 -req -in publisher.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out publisher.crt -days 3650

# 3. Generate postgres private key
openssl genrsa -out postgres.key 2048

# 5. Create postgres CSR with SAN
openssl req -new -key postgres.key -out postgres.csr -config postgres.cnf

# 6. Sign postgres certificate with CA and include SAN
openssl x509 -req -in postgres.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out postgres.crt -days 3650 -extensions v3_req -extfile postgres.cnf

rm -- *.csr

cd "$dir"
