#!/bin/bash
# A3: carga e escala. Uso: a3_run.sh <rotulo> <plantas> <intervalo_ms> <duracao_s>
cd $HOME/ep2
ROT=$1; N=$2; I=$3; D=$4
export MQTT_HOST=localhost MQTT_PORT=1883 MQTT_KEEPALIVE=10 MQTT_USE_TLS=0 POSTGRES_HOST=localhost POSTGRES_PORT=5432 POSTGRES_DB=monitoramento POSTGRES_USER=subscriber POSTGRES_PASSWORD=subscriber PGSSLMODE=disable PGPASSWORD=subscriber
R="setpriv --reuid=2000 --regid=2000 --clear-groups"
OUT=a3_$ROT.txt; : > $OUT
q(){ psql -h localhost -U subscriber -d monitoramento -q -c "$1" >> $OUT 2>&1; }
# limpa tabelas de carga anteriores
for t in $(psql -h localhost -U subscriber -d monitoramento -t -A -c "select tablename from pg_tables where tablename like 'caldeira_c%'"); do psql -h localhost -U subscriber -d monitoramento -q -c "drop table $t" ; done
$R repo/subscriber/bin/subscriber > a3_${ROT}_sub.out 2>&1 & S=$!; sleep 3
BIODATA_HOST=localhost a3/carga -n $N -i 1000 -d 1 > /dev/null; sleep 3   # cria as tabelas
psql -h localhost -U subscriber -d monitoramento -q -c "CREATE TABLE IF NOT EXISTS latencia_carga (planta text, time int8, gravado_em timestamptz); TRUNCATE latencia_carga; CREATE OR REPLACE FUNCTION reg_lat() RETURNS trigger AS \$f\$ BEGIN INSERT INTO latencia_carga VALUES (TG_TABLE_NAME, NEW.time, clock_timestamp()); RETURN NEW; END \$f\$ LANGUAGE plpgsql;" 
for t in $(psql -h localhost -U subscriber -d monitoramento -t -A -c "select tablename from pg_tables where tablename like 'caldeira_c%'"); do psql -h localhost -U subscriber -d monitoramento -q -c "DROP TRIGGER IF EXISTS trg ON $t; CREATE TRIGGER trg AFTER INSERT ON $t FOR EACH ROW EXECUTE FUNCTION reg_lat();"; done
sleep 2
A=$(date +%s); echo "cenario $ROT: $N plantas, intervalo $I ms, duracao $D s, inicio $A ($(date '+%F %T'))" >> $OUT
( while kill -0 $S 2>/dev/null; do ps -o %cpu=,rss= -p $S; sleep 10; done ) > a3_${ROT}_recursos.txt &
BIODATA_HOST=localhost a3/carga -n $N -i $I -d $D >> $OUT 2>&1
sleep 5; B=$(date +%s); echo "fim $B" >> $OUT
kill -INT $S; sleep 1
q "SELECT count(*) AS leituras_gravadas, count(DISTINCT planta) AS plantas, round(avg(extract(epoch FROM gravado_em) - time)::numeric,3) AS atraso_medio_s, round(max(extract(epoch FROM gravado_em) - time)::numeric,3) AS atraso_max_s, round((percentile_cont(0.95) WITHIN GROUP (ORDER BY extract(epoch FROM gravado_em) - time))::numeric,3) AS atraso_p95_s FROM latencia_carga WHERE time BETWEEN $A AND $B;"
q "SELECT min(n) AS min_por_planta, max(n) AS max_por_planta FROM (SELECT planta, count(*) n FROM latencia_carga WHERE time BETWEEN $A AND $B GROUP BY planta) s;"
echo "recursos do subscriber (cpu% e memoria RSS em kB, a cada 10 s):" >> $OUT
awk '{c+=$1; if($2>m)m=$2; if(NR==1)f=$2} END {printf "  amostras=%d cpu_media=%.1f%% rss_inicial=%d kB rss_max=%d kB\n", NR, c/NR, f, m}' a3_${ROT}_recursos.txt >> $OUT
echo "mensagens de erro no subscriber: $(grep -v NOTICE a3_${ROT}_sub.out | grep -ci error)" >> $OUT
cat $OUT
