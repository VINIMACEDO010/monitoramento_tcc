#!/bin/bash
# A5: execucao instrumentada (AddressSanitizer + UBSan), ~12 min
cd $HOME/ep2
export ASAN_OPTIONS=detect_leaks=1:log_path=$HOME/ep2/sanlogs/asan:halt_on_error=0
export UBSAN_OPTIONS=print_stacktrace=1:log_path=$HOME/ep2/sanlogs/ubsan
export MQTT_HOST=localhost MQTT_PORT=1883 MQTT_KEEPALIVE=10 MQTT_USE_TLS=0 POSTGRES_HOST=localhost POSTGRES_PORT=5432 POSTGRES_DB=monitoramento POSTGRES_USER=subscriber POSTGRES_PASSWORD=subscriber PGSSLMODE=disable PGPASSWORD=subscriber
L=a5_eventos.log; : > $L
ev(){ echo "[$(date '+%F %T')] $*" | tee -a $L; }
rm -f sanlogs/asan* sanlogs/ubsan*
ev "subscriber iniciado"; setpriv --reuid=2000 --regid=2000 --clear-groups ./asan/subscriber > a5_sub.out 2>&1 & SUB=$!
sleep 3
ev "publisher_mqtt iniciado"; BIODATA_HOST=localhost setpriv --reuid=2000 --regid=2000 --clear-groups ./asan/publisher_mqtt > a5_pubmqtt.out 2>&1 & PM=$!
sleep 240
ev "publisher_mqtt parado"; kill -INT $PM; wait $PM
ev "publisher_modbus iniciado"; BIODATA_HOST=localhost setpriv --reuid=2000 --regid=2000 --clear-groups ./asan/publisher_modbus > a5_pubmodbus.out 2>&1 & PB=$!
sleep 120
ev "publisher_modbus parado"; kill -INT $PB; wait $PB
ev "envios de teste: duplicado, planta nova, nome invalido"
BIODATA_HOST=localhost setpriv --reuid=2000 --regid=2000 --clear-groups ./asan/teste_envio -t 1700000100 -n 3 >> a5_teste.out 2>&1
BIODATA_HOST=localhost setpriv --reuid=2000 --regid=2000 --clear-groups ./asan/teste_envio -p caldeira_teste >> a5_teste.out 2>&1
BIODATA_HOST=localhost setpriv --reuid=2000 --regid=2000 --clear-groups ./asan/teste_envio -p 1caldeira >> a5_teste.out 2>&1
ev "publisher_mqtt reiniciado"; BIODATA_HOST=localhost setpriv --reuid=2000 --regid=2000 --clear-groups ./asan/publisher_mqtt >> a5_pubmqtt.out 2>&1 & PM=$!
sleep 30
ev "queda do broker por 15 s"; pkill -x mosquitto; sleep 15; mosquitto -c repo/mosquitto.conf -d; ev "broker de volta"
sleep 60
ev "queda do banco por 15 s"; su postgres -c "/usr/lib/postgresql/16/bin/pg_ctl -D $HOME/ep2/pg -m fast stop" >/dev/null; sleep 15; su postgres -c "/usr/lib/postgresql/16/bin/pg_ctl -D $HOME/ep2/pg -l /tmp/pg.log -o '-p 5432 -k /tmp' start" >/dev/null; ev "banco de volta"
sleep 120
ev "encerrando"; kill -INT $PM; wait $PM; kill -INT $SUB; wait $SUB; ev "subscriber saiu com codigo $?"
ev "FIM"
