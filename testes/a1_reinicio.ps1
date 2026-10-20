# Avaliacao A1 - Persistencia apos reinicio (Episodio 2)
# Uso, dentro da pasta monitoramento_tcc_publico:
#   1) powershell -ExecutionPolicy Bypass -File testes\a1_reinicio.ps1 -Etapa antes
#      (registra o estado do banco e desliga os conteineres SEM apagar os volumes)
#   2) reinicie o computador e abra o Docker Desktop
#   3) powershell -ExecutionPolicy Bypass -File testes\a1_reinicio.ps1 -Etapa depois
# Criterio de aceitacao (definido antes): a contagem de linhas e o primeiro e o ultimo instante
# gravados antes do desligamento sao os mesmos depois do reinicio, e a gravacao volta sozinha em ate 60 s.
# O resultado fica em testes\resultado_a1.txt
param([Parameter(Mandatory = $true)][ValidateSet("antes", "depois")][string]$Etapa)

$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$log = Join-Path $PSScriptRoot "resultado_a1.txt"
$estado = Join-Path $PSScriptRoot "a1_estado.txt"

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
function Valor([string]$q) { (docker compose exec -T postgres psql -U subscriber -d monitoramento -t -A -c $q 2>&1 | Out-String).Trim() }

if ($Etapa -eq "antes") {
    Log "A1 ETAPA ANTES"
    Registra "Versao do repositorio" (git describe --tags --always 2>&1)
    docker compose stop publisher-mqtt 2>&1 | Out-Null
    Start-Sleep -Seconds 6
    $linhas = Valor "SELECT count(*) FROM caldeira;"
    $primeiro = Valor "SELECT min(time) FROM caldeira;"
    $ultimo = Valor "SELECT max(time) FROM caldeira;"
    Set-Content -Path $estado -Value "$linhas;$primeiro;$ultimo" -Encoding ASCII
    Registra "Estado antes do desligamento" (Sql "SELECT count(*) AS linhas, to_timestamp(min(time)) AS primeiro, to_timestamp(max(time)) AS ultimo FROM caldeira;")
    Registra "Desligando os conteineres (sem -v, os volumes ficam)" (docker compose down 2>&1)
    Registra "Volumes que continuam existindo" (docker volume ls 2>&1)
    Log "Agora reinicie o computador, abra o Docker Desktop e rode a etapa depois."
} else {
    Log "A1 ETAPA DEPOIS"
    $s = (Get-Content $estado).Split(";")
    $linhas = [int64]$s[0]; $primeiro = [int64]$s[1]; $ultimo = [int64]$s[2]
    $subida = Agora
    Registra "Subindo o ambiente" (docker compose up -d 2>&1)
    $volta = $null
    for ($i = 0; $i -lt 36; $i++) {
        Start-Sleep -Seconds 5
        $novo = Valor "SELECT count(*) FROM caldeira WHERE time > $ultimo;"
        if ($novo -match '^\d+$' -and [int]$novo -gt 0) { $volta = (Agora) - $subida; break }
    }
    Registra "Conteineres em execucao" (docker compose ps 2>&1)
    Registra "Historico anterior ao desligamento, conferido depois do reinicio" (Sql "SELECT count(*) AS linhas, to_timestamp(min(time)) AS primeiro, to_timestamp(max(time)) AS ultimo FROM caldeira WHERE time <= $ultimo;")
    Log ("Antes do desligamento: linhas " + $linhas + ", primeiro " + $primeiro + ", ultimo " + $ultimo)
    $l2 = Valor "SELECT count(*) FROM caldeira WHERE time <= $ultimo;"
    $p2 = Valor "SELECT min(time) FROM caldeira;"
    $u2 = Valor "SELECT max(time) FROM caldeira WHERE time <= $ultimo;"
    Log ("Depois do reinicio:    linhas " + $l2 + ", primeiro " + $p2 + ", ultimo " + $u2)
    if ($volta) { Log ("Primeira leitura nova gravada " + $volta + " s depois do comando de subida") } else { Log "Nenhuma leitura nova gravada em 180 s" }
    Registra "Primeiras leituras depois do reinicio" (Sql "SELECT to_timestamp(time) AS instante, temp_fornalha, press_fornalha, vazao_caldeira, press_vapor FROM caldeira WHERE time > $ultimo ORDER BY time LIMIT 5;")
    Log "FIM DA A1"
}
