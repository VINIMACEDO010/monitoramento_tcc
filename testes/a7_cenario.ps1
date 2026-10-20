# Avaliacao A7 - Cenario de investigacao de falha (Episodio 2)
# Uso, dentro da pasta monitoramento_tcc_publico, com o sistema no ar:
#   1) powershell -ExecutionPolicy Bypass -File testes\a7_cenario.ps1 -Etapa injetar
#      (envia uma temperatura da fornalha de 1.450 C e registra o horario em arquivo; nao olhe o arquivo)
#   2) espere pelo menos 15 minutos
#   3) investigue pelo painel (capturas) e depois rode:
#      powershell -ExecutionPolicy Bypass -File testes\a7_cenario.ps1 -Etapa investigar
#   4) com o instante encontrado (numero da coluna time), rode:
#      powershell -ExecutionPolicy Bypass -File testes\a7_cenario.ps1 -Etapa janela -Instante <numero>
# Criterio de aceitacao (definido antes): achar, so pelo painel e por consulta SQL, o horario exato e o valor
# do desvio e as leituras dos 5 minutos antes e depois.
# O resultado fica em testes\resultado_a7.txt (o horario da injecao fica separado em a7_injecao.txt)
param([Parameter(Mandatory = $true)][ValidateSet("injetar", "investigar", "janela")][string]$Etapa, [int64]$Instante = 0)

$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$log = Join-Path $PSScriptRoot "resultado_a7.txt"

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

if ($Etapa -eq "injetar") {
    docker compose stop publisher-mqtt 2>&1 | Out-Null
    Start-Sleep -Seconds 6
    $t = Agora
    docker compose exec -T -e BIODATA_HOST=mosquitto subscriber /app/bin/teste_envio -t $t -T 1450 2>&1 | Out-Null
    docker compose start publisher-mqtt 2>&1 | Out-Null
    Add-Content -Path (Join-Path $PSScriptRoot "a7_injecao.txt") -Value ("Injecao de 1450 C no instante " + $t + " (" + (Get-Date -Format "yyyy-MM-dd HH:mm:ss") + ")") -Encoding UTF8
    Write-Host "Desvio enviado. Espere pelo menos 15 minutos antes de investigar." -ForegroundColor Yellow
} elseif ($Etapa -eq "investigar") {
    Log "A7 INVESTIGAR"
    Registra "Leituras fora das faixas nas ultimas 24 horas" (Sql "SELECT time, to_timestamp(time) AS instante, temp_fornalha, press_fornalha, vazao_caldeira, press_vapor FROM caldeira WHERE time > extract(epoch FROM now()) - 86400 AND (temp_fornalha NOT BETWEEN 1200 AND 1400 OR press_fornalha NOT BETWEEN -7 AND 0 OR vazao_caldeira NOT BETWEEN 10 AND 15 OR press_vapor NOT BETWEEN 13 AND 15) ORDER BY time;")
} else {
    Log ("A7 JANELA DE 5 MIN ANTES E DEPOIS DO INSTANTE " + $Instante)
    Registra "Leituras de 5 minutos antes e depois" (Sql "SELECT to_timestamp(time) AS instante, temp_fornalha, press_fornalha, vazao_caldeira, press_vapor FROM caldeira WHERE time BETWEEN $($Instante - 300) AND $($Instante + 300) ORDER BY time;")
    Registra "Resumo da janela" (Sql "SELECT count(*) AS leituras, round(avg(temp_fornalha)::numeric, 1) AS temp_media, max(temp_fornalha) AS temp_max, round(avg(press_vapor)::numeric, 2) AS pvap_media FROM caldeira WHERE time BETWEEN $($Instante - 300) AND $($Instante + 300);")
    Log "FIM DA A7"
}
