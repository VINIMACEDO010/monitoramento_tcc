# Campanha de testes do prototipo (repeticao dos cenarios e medicao de latencia)
# Uso, dentro da pasta monitoramento_tcc:
#   powershell -ExecutionPolicy Bypass -File testes\campanha_testes.ps1 -Minutos 1    (ensaio rapido, cerca de 20 min)
#   powershell -ExecutionPolicy Bypass -File testes\campanha_testes.ps1               (campanha completa, cerca de 3 h 15 min)
# O resultado fica em testes\resultados_campanha.txt
param([int]$Minutos = 30, [int]$Repeticoes = 3)

$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$log = Join-Path $PSScriptRoot "resultados_campanha.txt"

function Agora { [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() }
function Log([string]$t) {
    $l = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $t
    Write-Host $l -ForegroundColor Cyan
    Add-Content -Path $log -Value $l -Encoding UTF8
}
function Registra([string]$titulo, $saida) {
    Log (">> " + $titulo)
    $o = ($saida | Out-String)
    Write-Host $o
    Add-Content -Path $log -Value $o -Encoding UTF8
}
function Sql([string]$q) { docker compose exec -T postgres psql -U subscriber -d monitoramento -c $q 2>&1 }
function Envio([string[]]$opcoes) { docker compose exec -T -e BIODATA_HOST=mosquitto subscriber /app/bin/teste_envio @opcoes 2>&1 }

Log ("INICIO DA CAMPANHA: " + $Repeticoes + " repeticoes, coletas de " + $Minutos + " min")

# ---------- Preparacao ----------
Registra "Subindo o ambiente" (docker compose up -d --build 2>&1)
Registra "Compilando o publicador Modbus" (docker compose --profile modbus build publisher-modbus 2>&1)
docker compose --profile modbus stop publisher-modbus 2>&1 | Out-Null
Start-Sleep -Seconds 30
Registra "Conteineres em execucao (RNF01)" (docker compose ps 2>&1)
$gatilho = 'CREATE TABLE IF NOT EXISTS latencia (time int8, gravado_em timestamptz); CREATE OR REPLACE FUNCTION registra_latencia() RETURNS trigger AS $f$ BEGIN INSERT INTO latencia VALUES (NEW.time, clock_timestamp()); RETURN NEW; END $f$ LANGUAGE plpgsql; DROP TRIGGER IF EXISTS trg_latencia ON caldeira; CREATE TRIGGER trg_latencia AFTER INSERT ON caldeira FOR EACH ROW EXECUTE FUNCTION registra_latencia();'
Registra "Gatilho para medir a latencia" (Sql $gatilho)

# ---------- Coletas (RF01, RF02, RF03, RF04 e latencia) ----------
$script:janelas = @{ "MQTT" = @(); "Modbus" = @() }
function Coleta([string]$proto, [int]$n) {
    if ($proto -eq "MQTT") {
        docker compose --profile modbus stop publisher-modbus 2>&1 | Out-Null
        docker compose start publisher-mqtt 2>&1 | Out-Null
    } else {
        docker compose stop publisher-mqtt 2>&1 | Out-Null
        docker compose --profile modbus up -d publisher-modbus 2>&1 | Out-Null
    }
    Start-Sleep -Seconds 10
    $a = Agora
    Log ("Coleta " + $proto + " " + $n + " de " + $Repeticoes + " iniciada, instante " + $a)
    Start-Sleep -Seconds ($Minutos * 60)
    $b = Agora
    Log ("Coleta " + $proto + " " + $n + " encerrada, instante " + $b)
    $script:janelas[$proto] += "(time BETWEEN $a AND $b)"
    $q = "SELECT count(*) AS leituras, min(d) AS menor_intervalo_s, max(d) AS maior_intervalo_s, min(temp_fornalha) AS temp_min, max(temp_fornalha) AS temp_max, min(press_fornalha) AS pfor_min, max(press_fornalha) AS pfor_max, min(vazao_caldeira) AS vazao_min, max(vazao_caldeira) AS vazao_max, min(press_vapor) AS pvap_min, max(press_vapor) AS pvap_max FROM (SELECT *, time - lag(time) OVER (ORDER BY time) AS d FROM caldeira WHERE time BETWEEN $a AND $b) s;"
    Registra ("Resultado da coleta " + $proto + " " + $n) (Sql $q)
    $q = "SELECT count(*) AS leituras, round(avg(extract(epoch FROM gravado_em) - time)::numeric, 3) AS latencia_media_s, round(max(extract(epoch FROM gravado_em) - time)::numeric, 3) AS latencia_maxima_s FROM latencia WHERE time BETWEEN $a AND $b;"
    Registra ("Latencia da coleta " + $proto + " " + $n) (Sql $q)
}
for ($i = 1; $i -le $Repeticoes; $i++) { Coleta "MQTT" $i }
for ($i = 1; $i -le $Repeticoes; $i++) { Coleta "Modbus" $i }

# volta para o publicador MQTT
docker compose --profile modbus stop publisher-modbus 2>&1 | Out-Null
docker compose start publisher-mqtt 2>&1 | Out-Null
Start-Sleep -Seconds 15

# ---------- Protocolo aberto (RNF02) ----------
for ($i = 1; $i -le $Repeticoes; $i++) {
    Registra ("Cliente MQTT generico, repeticao " + $i) (docker compose exec -T mosquitto mosquitto_sub -h localhost -t caldeira/dados -C 3 -F "%I  %t  %l bytes" 2>&1)
}

# ---------- Descarte de duplicados (RF05) ----------
for ($i = 1; $i -le $Repeticoes; $i++) {
    $t = (Agora) - 31536000 - $i
    Envio @("-t", "$t", "-n", "3") | Out-Null
    Start-Sleep -Seconds 3
    Registra ("Mesma leitura enviada 3 vezes, repeticao " + $i + ", instante " + $t) (Sql "SELECT count(*) AS linhas_gravadas FROM caldeira WHERE time = $t;")
}

# ---------- Novas plantas e nome invalido (RNF08) ----------
for ($i = 1; $i -le $Repeticoes; $i++) {
    $planta = "caldeira_teste_" + $i + "_" + (Agora)
    Envio @("-p", $planta) | Out-Null
    Start-Sleep -Seconds 3
    Registra ("Planta nova " + $planta + ", repeticao " + $i) (Sql "SELECT count(*) AS linhas_na_tabela_nova FROM $planta;")
    Envio @("-p", "1caldeira") | Out-Null
    Start-Sleep -Seconds 3
    Registra ("Nome invalido 1caldeira, repeticao " + $i) (Sql "SELECT count(*) AS tabelas_1caldeira FROM pg_tables WHERE tablename = '1caldeira';")
}

# ---------- Separacao de privilegios (RNF03) ----------
for ($i = 1; $i -le $Repeticoes; $i++) {
    Registra ("INSERT com o usuario readonly, repeticao " + $i) (docker compose exec -T postgres psql -U readonly -d monitoramento -c "INSERT INTO caldeira (time) VALUES (1);" 2>&1)
}

# ---------- Autenticacao (RNF04) e usuario do processo (RNF05) ----------
Registra "Senhas e regras de autenticacao (RNF04)" (Sql "SELECT rolname, left(rolpassword, 14) AS senha FROM pg_authid WHERE rolname IN ('subscriber','readonly');")
Registra "Regras de conexao (RNF04)" (Sql "SELECT line_number, type, database, user_name, auth_method FROM pg_hba_file_rules;")
Registra "Usuario do processo do subscriber (RNF05)" (docker compose top subscriber 2>&1)

# ---------- Identificacao de desvio (RF07, RN01) ----------
docker compose stop publisher-mqtt 2>&1 | Out-Null
Start-Sleep -Seconds 6
for ($i = 1; $i -le $Repeticoes; $i++) {
    Envio @("-T", "1450") | Out-Null
    Start-Sleep -Seconds 3
    Registra ("Leitura de 1450 C enviada, repeticao " + $i) (Sql "SELECT time, temp_fornalha FROM caldeira ORDER BY time DESC LIMIT 1;")
    if ($i -eq 1) {
        Log "PAINEL EM DESVIO: se estiver no computador, tire agora o print do Grafana (mostrador vermelho). Aguardando 60 s..."
        Start-Sleep -Seconds 60
    } else { Start-Sleep -Seconds 10 }
}
docker compose start publisher-mqtt 2>&1 | Out-Null
Start-Sleep -Seconds 20

# ---------- Recuperacao de falhas (RNF06) ----------
function Queda([string]$servico, [int]$n) {
    $a = Agora
    Log ("Queda do servico " + $servico + ", repeticao " + $n + ", parado por 10 s")
    docker compose stop $servico 2>&1 | Out-Null
    Start-Sleep -Seconds 10
    docker compose start $servico 2>&1 | Out-Null
    Start-Sleep -Seconds 50
    $b = Agora
    $q = "SELECT max(d) AS maior_intervalo_s, count(*) AS leituras_na_janela FROM (SELECT time - lag(time) OVER (ORDER BY time) AS d FROM caldeira WHERE time BETWEEN " + ($a - 30) + " AND $b) s;"
    Registra ("Interrupcao registrada na queda do " + $servico + ", repeticao " + $n) (Sql $q)
}
for ($i = 1; $i -le $Repeticoes; $i++) { Queda "mosquitto" $i }
for ($i = 1; $i -le $Repeticoes; $i++) { Queda "postgres" $i }
Registra "Log do subscriber apos as quedas" (docker compose logs --tail 40 subscriber 2>&1)

# ---------- Encerramento ----------
foreach ($proto in @("MQTT", "Modbus")) {
    $filtro = $script:janelas[$proto] -join " OR "
    Registra ("Resumo da latencia nas coletas " + $proto) (Sql ("SELECT count(*) AS leituras, round(avg(extract(epoch FROM gravado_em) - time)::numeric, 3) AS media_s, round(min(extract(epoch FROM gravado_em) - time)::numeric, 3) AS minima_s, round(max(extract(epoch FROM gravado_em) - time)::numeric, 3) AS maxima_s FROM latencia WHERE " + $filtro + ";"))
}
Registra "Removendo o gatilho de latencia" (Sql "DROP TRIGGER IF EXISTS trg_latencia ON caldeira;")
Log "FIM DA CAMPANHA"
