# Registros da avaliação (Episódio 2)

Registros das avaliações A1 a A5 e A7 descritas na seção 5.6.2 do TCC, gerados com a versão
`v1.0-tcc` do protótipo. As linhas com caminhos de pastas do computador usado nos testes foram
removidas, sem alterar nenhum valor medido.

| Avaliação | Ambiente | Registros (`registros/`) | Ferramentas |
|---|---|---|---|
| A1. Persistência após reinício | Windows 11 + Docker Desktop 29.6.1 | `a1_etapa_antes_e_tentativa1_projeto_errado.txt` (estado antes do desligamento e primeira tentativa, feita sem `COMPOSE_PROJECT_NAME`) e `a1_etapa_depois.txt` | `testes/a1_reinicio.ps1` |
| A2. Execução longa (8 h) | Windows 11 + Docker Desktop 29.6.1 | `a2_execucao_longa.txt` (ensaio de 3 min seguido da execução de 8 h) | `testes/a2_execucao_longa.ps1` |
| A3. Carga e escala | Ubuntu 24.04, sem Docker | `a3_carga_*.txt` | `ferramentas/carga.c`, `ferramentas/a3_run.sh` |
| A4. Interoperabilidade | Ubuntu 24.04, sem Docker | `a4_interoperabilidade.txt` | `ferramentas/le_modbus.py`, `ferramentas/escreve_modbus.py` (pymodbus 3.16.1) |
| A5. Qualidade do código | Ubuntu 24.04, gcc 13, cppcheck 2.13 | `a5_cppcheck.txt`, `a5_memoria.txt`, `a5_eventos.log` | `ferramentas/a5_run2.sh` |
| A7. Cenário de investigação | Windows 11 + Docker Desktop 29.6.1 | `a7_cenario.txt` e `a7_injecao.txt` (execução válida), `*_tentativa1.txt` (descartada) | `testes/a7_cenario.ps1` |

Nos scripts em PowerShell, defina `COMPOSE_PROJECT_NAME` se a pasta do repositório tiver outro nome
que não `monitoramento_tcc`, para que o Docker Compose use os mesmos volumes.
