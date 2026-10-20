# Avaliacao A2 - Execucao longa (Episodio 2)
# Uso, dentro da pasta monitoramento_tcc_publico, com o sistema ja no ar (docker compose up -d):
#   powershell -ExecutionPolicy Bypass -File testes\a2_execucao_longa.ps1            (8 horas)
#   powershell -ExecutionPolicy Bypass -File testes\a2_execucao_longa.ps1 -Horas 0.1 (ensaio de 6 minutos)
# Criterio de aceitacao (definido antes): pelo menos 99% das leituras esperadas,
# nenhum intervalo maior que 10 s e memoria do subscriber sem crescimento continuo.
# O resultado fica em testes\resultado_a2.txt
param([double]$Horas = 8, [int]$MinutosEntreAmostras = 10)

$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$log = Join-Path $PSScriptRoot "resultado_a2.txt"

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
function Stats { docker stats --no-stream --format "table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}" 2>&1 }

Log ("A2 EXECUCAO LONGA: " + $Horas + " h, amostra de recursos a cada " + $MinutosEntreAmostras + " min")
Registra "Versao do repositorio" (git describe --tags --always 2>&1)
Registra "Docker" (docker version --format "Cliente {{.Client.Version}} / Servidor {{.Server.Version}}" 2>&1)
Registra "Conteineres em execucao" (docker compose ps 2>&1)
Registra "Tamanho do banco no inicio" (Sql "SELECT pg_size_pretty(pg_database_size('monitoramento')) AS banco, pg_size_pretty(pg_total_relation_size('caldeira')) AS tabela_caldeira, (SELECT count(*) FROM caldeira) AS linhas;")

$a = Agora
Log ("Inicio da janela, instante " + $a)
$fim = (Get-Date).AddHours($Horas)
$n = 0
while ((Get-Date) -lt $fim) {
    $n++
    Registra ("Recursos, amostra " + $n) (Stats)
    $resta = ($fim - (Get-Date)).TotalSeconds
    Start-Sleep -Seconds ([math]::Max(1, [math]::Min($MinutosEntreAmostras * 60, $resta)))
}
$b = Agora
Log ("Fim da janela, instante " + $b)
Registra "Recursos, amostra final" (Stats)

$esperadas = [math]::Floor(($b - $a) / 5) + 1
$q = "SELECT count(*) AS leituras, $esperadas AS esperadas, round(100.0 * count(*) / $esperadas, 2) AS percentual, min(d) AS menor_intervalo_s, max(d) AS maior_intervalo_s, count(*) FILTER (WHERE d > 10) AS intervalos_acima_10s, count(*) FILTER (WHERE d <> 5) AS intervalos_diferentes_de_5s FROM (SELECT time, time - lag(time) OVER (ORDER BY time) AS d FROM caldeira WHERE time BETWEEN $a AND $b) s;"
Registra "Resultado da execucao longa" (Sql $q)
$q = "SELECT to_timestamp(time) AS depois_de, d AS intervalo_s FROM (SELECT time, time - lag(time) OVER (ORDER BY time) AS d FROM caldeira WHERE time BETWEEN $a AND $b) s WHERE d <> 5 ORDER BY time;"
Registra "Intervalos fora de 5 s (se houver)" (Sql $q)
Registra "Tamanho do banco no fim" (Sql "SELECT pg_size_pretty(pg_database_size('monitoramento')) AS banco, pg_size_pretty(pg_total_relation_size('caldeira')) AS tabela_caldeira, (SELECT count(*) FROM caldeira) AS linhas;")
Registra "Log do subscriber (ultimas 20 linhas)" (docker compose logs --tail 20 subscriber 2>&1)
Log "FIM DA A2"
