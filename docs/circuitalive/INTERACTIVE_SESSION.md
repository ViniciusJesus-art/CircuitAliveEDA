# Sessão interativa — etapa 1

Implementada sobre KiCad 10.0.7, no Fedora/Linux. Esta entrega é o núcleo testável
do motor; os botões e indicadores no esquemático pertencem à etapa 2.

## Comportamento implementado

- `Configure`: recebe um circuito com modelos inline e os sinais observados.
- `Play`: inicia uma análise nova ou retoma uma sessão pausada.
- `Pause`: só confirma a pausa depois que o motor terminou de avançar.
- `AdvanceTo`: avança até um tempo absoluto e pausa, conservando o estado do solver.
- `SetSourceValue`: altera uma fonte V/I durante uma pausa confirmada. Requisições
  são aplicadas em ordem, no mesmo tempo simulado, sem reinicializar capacitores.
- `Stop`: encerra e recolhe o processo antigo, preparando um worker novo com o
  circuito original. O próximo Play começa nas condições iniciais, inclusive
  depois de uma análise concluída ou de uma falha do worker.
- `Snapshot`: copia tempo, valores, geração da sessão, histórico limitado e
  métricas. Uma geração nova invalida resultados de uma sessão encerrada.

Estados: `STOPPED`, `RUNNING`, `PAUSED`, `COMPLETED`, `ERROR`.
As chamadas do controlador são síncronas e serializadas. A futura interface deve
executá-las em sua thread de controle e publicar os snapshots para a thread wx.
Callbacks do ngspice não acessam objetos do editor nem objetos gráficos.

## Por que usar um processo auxiliar

Os ensaios com libngspice 47 confirmaram dois pontos relevantes:

1. `bg_halt`/`bg_resume` preservam o estado do circuito; repetir `bg_run` não é
   uma implementação de retomada.
2. `.save none` disponibiliza as amostras por callbacks mantendo um ponto por
   vetor. Essa opção persiste dentro da biblioteca, mesmo após remover o circuito
   e selecionar `save all`.

Por isso, o novo modo usa `circuitalive-sim-worker`, com um processo por sessão.
Ele não carrega a mesma instância da biblioteca usada pelo simulador tradicional
do KiCad. O wrapper existente em `ngspice.cpp` foi preservado. Falha ou travamento
do worker resulta em erro controlado, término e coleta do processo, sem derrubar
o editor. O transporte usa um socket local anônimo e mensagens JSON limitadas;
não abre porta de rede nem executa comandos por shell.

O Stop descarta o processo inteiro porque os ensaios também encontraram estado
residual ao reutilizar o motor depois de atingir o fim da análise. Pause e
retomada continuam usando exatamente o mesmo processo e estado elétrico.

Referências: [API oficial de biblioteca compartilhada](https://ngspice.sourceforge.io/shared.html),
[manual](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf) e
[implementação upstream de armazenamento](https://github.com/imr/ngspice/blob/master/src/frontend/outitf.c).

## Continuidade e limites desta entrega

O horizonte da análise é **finito e explícito**, escolhido em `SIM_SESSION_CONFIG`.
`AdvanceTo` continua a mesma análise por vários trechos até esse horizonte.
Ao alcançá-lo, a sessão passa a `COMPLETED`; pedidos além dele são recusados.
Não há extensão automática do `Tstop` nem reinício oculto para aparentar
continuidade. O protótipo não prova execução indefinida, suporte a todos os
modelos ou comportamento de memórias digitais.

O histórico do controlador é um buffer circular, com capacidade configurável.
O ngspice mantém um ponto por vetor no modo de streaming. O contador de amostras
é independente do tamanho dos vetores. O tamanho nativo é medido somente com o
motor pausado; `engineRetainedPoints == 0` durante execução significa não medido.
O worker também limpa periodicamente as estruturas de comandos do interpretador,
que são outra fonte de crescimento de memória, separada dos vetores.

Nesta fase, o circuito usa modelos inline. Diretivas de análises, `.control`,
`.save`, `.include` e `.lib` não são aceitas na entrada do controlador: a sessão
administra a análise, o armazenamento e os comandos. Catálogo de modelos,
arquivos externos, eventos digitais/XSPICE e associação ao desenho são trabalho
posterior. O modelo de chave do exemplo é o dispositivo analógico SW do ngspice.

## Validação executada

Os 13 casos de `qa_interactive_sim` passaram com libngspice 47:

- RC comparado à solução analítica `1 - exp(-t/RC)`, inclusive corrente da fonte.
- Tempo e tensão congelados durante Pause; retomada sem reinicialização.
- Play em background e reconhecimento da parada antes de retornar.
- Mudanças ordenadas de fonte; descarga do capacitor a partir da tensão existente.
- Chave analógica e LED respondendo à corrente calculada, sem lógica visual simulada.
- Stop/Play reproduzindo as condições iniciais, inclusive após `COMPLETED`.
- Histórico limitado, descarte de amostras antigas e 100 trechos na mesma geração.
- Erros de entrada, sinal ausente e recuperação com Stop.
- Sessões independentes, encerramento durante execução e ausência de processos zumbis.
- Worker inexistente, morto ou suspenso; timeout e recuperação.
- Caminho do executável contendo espaços e inicialização com stdin fechado.

Os 53 casos originais de `qa_spice` também passaram após a implementação.

Ensaio de memória do circuito RC, com passo máximo de 1 µs:

| Medida | Resultado |
|---|---|
| Trechos consecutivos | 1.000 |
| Tempo simulado | 10 s |
| Amostras recebidas | 10.003.008 |
| Histórico do controlador | até 64 amostras |
| Pontos mantidos por vetor no ngspice | 1 |
| RSS após aquecimento | 13.361.152 bytes, cerca de 12,7 MiB |
| Variação observada do RSS após aquecimento | 0 bytes |
| Tempo de parede do ensaio nesta máquina | 15,84 s |

Essa medição é específica do RC e deste ambiente; não é uma garantia de memória
ou velocidade para circuitos grandes, linhas de transmissão ou modelos digitais.

## Reproduzir

```bash
tools/circuitalive/configure.sh
tools/circuitalive/build.sh --target qa_interactive_sim qa_spice
tools/circuitalive/test-interactive.sh --verbose
tools/circuitalive/test-spice.sh --verbose
python3 tools/circuitalive/benchmark-interactive.py --windows 1000
```

O worker é instalado por `tools/circuitalive/install.sh` em
`.circuitalive/install/bin/circuitalive-sim-worker`. O controlador recebe seu
caminho explicitamente; os testes usam a cópia do build. A instalação pode ser
verificada com:

```bash
python3 tools/circuitalive/benchmark-interactive.py --windows 10 \
  --worker "$PWD/.circuitalive/install/bin/circuitalive-sim-worker" \
  --output .circuitalive/logs/interactive-installed.json
```

O exemplo com chave e LED está em
`demos/simulation/circuitalive/rc_switch_led.cir`. É um deck de componentes para
o controlador, ainda sem uma folha KiCad ou representação gráfica interativa.

Arquivos principais: `eeschema/sim/interactive_sim_session.h/.cpp`,
`interactive_sim_worker.cpp`, `interactive_sim_protocol.h` e
`qa/tests/spice/test_interactive_sim_session.cpp`.

Evidências locais ignoradas pelo Git: `.circuitalive/logs/test-interactive-final.log`,
`test-spice-after-interactive.log`, `interactive-memory.json` e
`interactive-memory-summary.json`. O subdiretório `experiments/interactive`
contém os ensaios de API usados para verificar as limitações encontradas.
