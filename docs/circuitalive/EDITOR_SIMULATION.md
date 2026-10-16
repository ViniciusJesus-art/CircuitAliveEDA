# Simulação no esquemático — etapa 2

O editor possui uma barra CircuitAlive com **Play, Pause e Stop**, tempo simulado
e corrente dos LEDs. A preparação usa o circuito desenhado e o grafo elétrico do
KiCad. Nenhuma janela de análise ou duração precisa ser preenchida para o conjunto
de componentes suportado.

## Experimentar

```bash
./circuitalive eeschema demos/simulation/circuitalive/interactive_rc_led.kicad_sch
```

1. Pressione Play. A chave começa fechada; C1 carrega e D1 acende gradualmente.
2. Clique no corpo de SW1. O contorno e ON/OFF indicam a posição elétrica;
   o capacitor descarrega e o LED perde brilho conforme sua corrente diminui.
3. Pause congela o tempo e o estado elétrico. É possível mudar a chave durante
   a pausa; o efeito elétrico aparece ao retomar com Play.
4. Stop descarta a sessão. O próximo Play começa em zero, com C1 descarregado
   e a chave fechada.
5. Uma edição confirmada do esquema encerra a sessão. A barra pede novo Play
   para reconstruir o circuito. Trocar a variante também invalida a sessão.

Os cliques de chave alteram somente a sessão. Não modificam o arquivo, o valor do
símbolo nem o histórico de desfazer. Fora da sessão, a seleção e edição seguem
o comportamento normal do KiCad. A navegação entre folhas conserva a sessão e
desenha apenas os indicadores da instância visível.

## Catálogo inicial

| Símbolo da biblioteca | Comportamento nesta etapa |
|---|---|
| `Device:R`, `R_US`, `R_Small` | Resistor positivo, valor do campo Value |
| `Device:C`, `C_Small` | Capacitor positivo, condição inicial de 0 V |
| `Simulation_SPICE:VDC` | Fonte DC, valor do campo Value, pino 1 positivo |
| `Switch:SW_SPST` | Chave analógica controlada por clique, inicialmente fechada |
| `Device:LED`, `LED_Small`, com Value `RED` ou `LED_RED` | Modelo didático de LED vermelho, pino 1 cátodo e 2 ânodo |

A barra mostra a corrente de até três LEDs e a quantidade restante; todos os
LEDs suportados continuam recebendo indicação de brilho no desenho.

Os componentes precisam estar anotados e conectados. Uma rede `GND` ou `0` define
a referência elétrica; símbolos `power:*` são rótulos elétricos, não fontes de
tensão. A captura respeita exclusões de simulação e instancia as folhas usando
o grafo de conectividade. Nomes de redes são convertidos em identificadores
internos; texto do esquema nunca é inserido como comando no netlist.

Valores usam ponto decimal e sufixos SPICE: `330`, `10k`, `1000u`, `100uF`,
`2.2Meg`. `M` significa mili; mega é `Meg`. Notações não reconhecidas geram erro.
Modelos SPICE personalizados, pinagens diferentes e componentes fora do catálogo
são recusados com a referência do componente; não há substituição automática.
Diretivas SPICE em textos também são recusadas; remova/exclua esses textos do
modo interativo ou use o simulador tradicional para análises personalizadas.

O modelo do LED é `D(Is=1e-20 N=2 Rs=5)` e o da chave
`SW(Ron=0.01 Roff=1e12 Vt=2.5 Vh=0.1)`. São modelos didáticos próprios, sem
pretensão de reproduzir um fabricante. O brilho usa corrente direta, com resposta
visual `sqrt(clamp(I / 10mA, 0, 1))`; corrente reversa não acende o LED. A média das
amostras retidas em cada trecho suaviza a apresentação. Isso ainda não é uma
integração de PWM de alta frequência nem um modelo de dano/sobreaquecimento.

## Organização para atualizar o KiCad

Todo o código novo da integração fica em `eeschema/circuitalive/`:

| Arquivo | Responsabilidade |
|---|---|
| `circuit_snapshot.*` | Catálogo, validação, valores e netlist, sem wx/KiCad |
| `schematic_adapter.*` | Captura do grafo, campos e instâncias do KiCad, somente na thread gráfica |
| `session_runner.*` | Thread proprietária da sessão, fila limitada e estados copiados para a interface |
| `simulation_overlay.*` | Brilho do LED, indicação da chave e localização do clique, sem mudar símbolos |
| `editor_simulation.*` | Ações próprias, ferramenta, barra, timer e ciclo de vida |
| `CMakeLists.txt` | Compilação e ligação dos módulos |

O núcleo e o worker continuam nos arquivos `interactive_sim_*` da etapa 1, em
`eeschema/sim/`. O simulador tradicional mantém seu próprio motor e suas ações.

Os pontos de conexão com arquivos anteriores do KiCad são intencionalmente pequenos:

- `eeschema/CMakeLists.txt`: inclusão do diretório `circuitalive`.
- `eeschema/sch_edit_frame.cpp`: inclusão do cabeçalho; registro da ferramenta;
  instalação da barra após a interface; shutdown antes de destruir o esquema;
  invalidação em `OnModify` e reconexão ao substituir o documento.
- `qa/tests/spice/CMakeLists.txt`: conexão dos testes dos módulos.

Não foi necessário modificar `SCH_SYMBOL`, `SCH_PIN`, `SCH_ACTIONS`, o painter,
os formatos de arquivo ou o wrapper ngspice original. A barra é um painel AUI
próprio e aparece também com configurações de toolbar já existentes. As três
ações usam o namespace `circuitalive.Interactive.*`.

Ao migrar a base, primeiro revalide os pontos de conexão e a API utilizada pelo
`schematic_adapter`; depois compile e execute os testes de conectividade e motor.
Esta organização reduz conflitos, mas não garante atualização sem ajustes se
as APIs do KiCad mudarem.

## Execução e limites

Somente a thread gráfica lê o esquema e desenha os resultados. A thread de
controle recebe cópias e executa as chamadas bloqueantes do worker isolado.
Um timer de 33 ms apresenta snapshots; o controle avança em trechos de 20 ms
simulados, com pausas entre trechos, sem acumular trabalho para recuperar atraso
de parede. Circuitos lentos podem avançar abaixo do tempo real.

Os parâmetros internos desta etapa são passo máximo de **100 µs**, horizonte
finito de **1 hora simulada**, histórico circular de 256 amostras e até 64 LEDs.
O horizonte não é estendido automaticamente: o estado muda para Completed ao
atingi-lo. Play depois da conclusão começa uma sessão nova; não é continuação
indefinida. Circuitos rápidos e lógica digital ainda exigem as próximas etapas.

A fila de comandos possui limite de 128 entradas; Stop cancela as pendências.
Revisões impedem que resultados de uma sessão descartada reapareçam na interface.
O encerramento do editor recolhe a thread e o processo. Em falha/travamento,
vale o timeout limitado do controlador da etapa 1. A integração atual é Linux;
em outras plataformas os pontos de conexão são inativos.

## Verificação

```bash
tools/circuitalive/build.sh --target eeschema qa_interactive_sim qa_spice
tools/circuitalive/test-interactive.sh --verbose
tools/circuitalive/test-spice.sh --verbose
tools/circuitalive/install.sh
```

Os testes adicionais verificam os valores e recusas do catálogo, polaridade do
LED, alteração de corrente com outro resistor, fila da interface, pausa,
retomada, clique em pausa, eventos ordenados, Stop/Play, cancelamento, conclusão
finita e falha do worker. O teste do adaptador carrega o arquivo `.kicad_sch`
da demonstração, captura as conexões reais e simula carga/descarga. A interface
também deve ser verificada em uma sessão gráfica, incluindo edição e fechamento
com a sessão ativa.

### Resultado nesta máquina

- `qa_interactive_sim`: **20 casos aprovados**, 1,99 s.
- `qa_spice`: **54 casos aprovados**, 2,10 s; inclui os 53 anteriores e a captura
  do esquemático da demonstração.
- Interface verificada em Xvfb, separada da área de trabalho do usuário:
  Play, Pause, clique na chave durante pausa, retomada, Stop/Play, invalidação
  ao rotacionar um componente, desfazer e fechamento com a sessão executando.
- Pause manteve `t = 0,900 s` e `I(D1) = 4,13 mA`; abrir SW1 não alterou os
  valores congelados. Ao retomar, a corrente caiu para `0,06 mA` em `t = 2,200 s`.
- Stop mostrou `t = 0 s`; novo Play reiniciou a carga com a chave fechada.
- Uma edição mostrou `Circuit changed — press Play to rebuild`.
- Encerramento normal com a sessão ativa: **exit code 0**, sem mensagens de erro.

Evidências locais em `.circuitalive/logs/`: `test-editor-interactive-final.log`,
`test-editor-spice-final.log`, `editor-ui-results.json`, capturas `editor-*.png`
e `gui-editor-exit.txt`. São artefatos gerados e não fazem parte do Git.
