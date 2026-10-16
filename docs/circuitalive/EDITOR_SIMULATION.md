# Simulação no esquemático — etapa 2

**Atualização:** os comportamentos agora vêm de arquivos de modelo e podem ser
configurados por componente. Veja [Arquivos de modelo](MODEL_FILES.md) para o
funcionamento atual; as validações históricas ao final registram as etapas anteriores.

O editor possui ícones **Play, Pause e Stop no final da barra superior original**,
com o mesmo tamanho e comportamento dos outros botões. Tempo, estado e corrente
dos LEDs aparecem na barra de status inferior existente. A preparação usa o circuito desenhado e o grafo elétrico do
KiCad. Nenhuma janela de análise ou duração precisa ser preenchida para o conjunto
de componentes suportado.

## Experimentar

```bash
./circuitalive eeschema demos/simulation/circuitalive/interactive_rc_led.kicad_sch
```

1. Pressione o ícone Play. A chave começa fechada; C1 carrega e D1 acende gradualmente.
2. Clique no corpo de SW1. O contorno e ON/OFF indicam a posição elétrica;
   o capacitor descarrega e o LED perde brilho conforme sua corrente diminui.
3. Pause congela o tempo e o estado elétrico. É possível mudar a chave durante
   a pausa; o efeito elétrico aparece ao retomar com Play.
4. Stop descarta a sessão. O próximo Play começa em zero, com C1 descarregado
   e a chave fechada.
5. Uma edição confirmada do esquema encerra a sessão. O status pede novo Play
   para reconstruir o circuito. Trocar a variante também invalida a sessão.

Os cliques de chave alteram somente a sessão. Não modificam o arquivo, o valor do
símbolo nem o histórico de desfazer. Fora da sessão, a seleção e edição seguem
o comportamento normal do KiCad. A navegação entre folhas conserva a sessão e
desenha apenas os indicadores da instância visível.

## Catálogo inicial

| Símbolo da biblioteca | Comportamento nesta etapa |
|---|---|
| `Device:R`, `R_US`, `R_Small` | Resistor positivo, valor do campo Value |
| `Device:C`, `C_Small`, `C_US`, `C_Polarized`, `C_Polarized_Small`, `C_Polarized_US` | Capacitor ideal positivo, condição inicial de 0 V |
| `Simulation_SPICE:VDC`, `Device:Battery`, `Device:Battery_Cell` | Fonte DC, valor do campo Value, pino 1 positivo |
| `Switch:SW_SPST` | Chave analógica controlada por clique, inicialmente fechada |
| `Device:LED`, `LED_Small` | Modelo didático vermelho inicial, pino 1 cátodo e 2 ânodo; não precisa renomear Value |

O status mostra a corrente de até três LEDs e a quantidade restante; todos os
LEDs suportados continuam recebendo indicação de brilho no desenho.

Os símbolos são os das bibliotecas padrão do KiCad. Não há biblioteca separada
de componentes CircuitAlive: `simulation_model.*` lê os arquivos que associam o ID de biblioteca ao
comportamento elétrico inicial. Os arquivos distribuídos de R e C usam por padrão comportamento ideal e o campo
Value, sem exigir configuração SPICE. Campos `Sim.*` existentes nesses passivos
não os tornam incompatíveis nem substituem seu Value neste modo. Outro arquivo de modelo pode ser associado pela janela Simulation model.

Isso também se aplica a cópias em bibliotecas locais do projeto: por exemplo,
`rectifier_schlib:R` e `rectifier_schlib:C`. O nome local R/C ou o campo
`Sim.Device=R/C` identifica o comportamento básico; o modelo valida a existência
dos dois pinos físicos 1 e 2. O nickname da biblioteca não é uma propriedade
elétrica. Símbolos de alimentação são identificados também por seu tipo KiCad,
permitindo GND copiado para uma biblioteca local.

Exemplo mínimo, equivalente a uma fonte de 5 V, resistor de 470 ohms e LED,
incluindo campos `Sim.*` no resistor para reproduzir a regressão:

```bash
./circuitalive eeschema demos/simulation/circuitalive/standard_library_led.kicad_sch
```

Os componentes precisam estar anotados; pinos abertos são permitidos. Uma rede
`GND` ou `0`, quando presente, define a referência elétrica. Na ausência dela,
a sessão escolhe o negativo de uma fonte DC como 0 V para cada ilha elétrica
independente. Não é preciso desenhar uma label GND. Fontes em série conservam
terminais distintos e suas tensões se somam; não são aterrados todos os negativos.
Símbolos de alimentação são rótulos elétricos, não fontes de tensão.
A captura respeita exclusões de simulação e instancia as folhas usando
o grafo de conectividade. Nomes de redes são convertidos em identificadores
internos; texto do esquema nunca é inserido como comando no netlist.

Cada pino fisicamente existente, mas aberto, recebe seu próprio nó. Não é
curto-circuitado com outros pinos abertos nem automaticamente interpretado como
nível lógico baixo. Um resistor ligado a +5 V e aberto na outra ponta apresenta
aproximadamente 5 V nessa ponta, com corrente praticamente nula. Já uma ilha
inteiramente isolada não tem tensão absoluta definida: o status indica
`Floating` e a captura mantém a lista dessas ilhas. Isso não implementa ainda
estados digitais X/Z, que pertencem às próximas etapas.

O solver usa uma referência numérica por ilha e `rshunt=1e12` para estabilizar
nós sem caminho DC. Trata-se de uma condutância muito pequena, não de um fio
de GND; ela pode influenciar circuitos de impedância extremamente alta. Esse
recurso e suas limitações são descritos no
[manual oficial do ngspice, condicionamento de matriz](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).

Exemplo com o resistor local real, fonte sem label GND e R2 com um terminal aberto:

```bash
./circuitalive eeschema demos/simulation/circuitalive/open_pins_auto_reference.kicad_sch
```

Valores usam ponto decimal e unidades KiCad/SI: `470R`, `4R7`, `4k7`, `1000u`,
`100uF`, `2.2Meg`. `M` significa mega e `m` significa mili, como no campo Value
do KiCad. O netlist recebe o número convertido. Notações não reconhecidas geram erro.
Modelos próprios e pinagens diferentes podem ser associados por um arquivo `.casim`.
Campos SPICE antigos personalizados em dispositivos além de R/C pedem associação
explícita. Um componente sem arquivo associado é identificado antes de iniciar
a simulação; veja [Arquivos de modelo](MODEL_FILES.md).
Diretivas SPICE em textos também são recusadas; remova/exclua esses textos do
modo interativo ou use o simulador tradicional para análises personalizadas.

O LED usa um modelo de diodo configurável por tensão direta na corrente de
referência; a chave usa por padrão `SW(Ron=0.01 Roff=1e12 Vt=2.5 Vh=0.1)`.
São modelos didáticos próprios, sem
pretensão de reproduzir um fabricante. O brilho usa corrente direta, com resposta
visual `sqrt(clamp(I / I_brilho, 0, 1))`, com 10 mA por padrão e escala configurável;
corrente reversa não acende o LED. A média das
amostras retidas em cada trecho suaviza a apresentação. Isso ainda não é uma
integração de PWM de alta frequência nem um modelo de dano/sobreaquecimento.

## Organização para atualizar o KiCad

Todo o código novo da integração fica em `eeschema/circuitalive/`:

| Arquivo | Responsabilidade |
|---|---|
| `circuit_snapshot.*` | Validação, valores e netlist, sem wx/KiCad |
| `simulation_model.*` | Arquivos, associação, terminais, parâmetros e templates do modelo |
| `model_dialog.*` | Configuração por componente no editor |
| `circuit_topology.*` | Nós, pinos abertos, ilhas elétricas e prioridade entre GND explícito e referência automática |
| `schematic_adapter.*` | Captura do grafo, campos e instâncias do KiCad, somente na thread gráfica |
| `session_runner.*` | Thread proprietária da sessão, fila limitada e estados copiados para a interface |
| `simulation_overlay.*` | Brilho do LED, indicação da chave e localização do clique, sem mudar símbolos |
| `editor_simulation.*` | Ferramenta, estado, timer e ciclo de vida |
| `simulation_toolbar.*` | Ações e integração na toolbar original; ícone vetorial Pause local |
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
os formatos de arquivo ou o wrapper ngspice original. A integração usa os botões
nativos `ACTION_TOOLBAR` e acrescenta as ações ao final da toolbar principal,
inclusive após sua recriação ou personalização. As três ações usam o namespace
`circuitalive.Interactive.*`. Não cria painel nem barra extra e não exibe nome
do projeto junto dos botões.

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

### Validação inicial da etapa 2

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

### Correção para componentes padrão e toolbar nativa

- `qa_interactive_sim`: **22 casos aprovados**, incluindo variantes de R/C,
  valores `470R`, `4k7`, `4R7` e a distinção SI entre `M` e `m`.
- `qa_spice`: **56 casos aprovados**, incluindo o resistor com `Sim.Device`,
  `Sim.Pins` e `Sim.Params`, o capacitor com metadados diferentes de seu Value e
  o exemplo mínimo de fonte, resistor, LED e GND carregado do arquivo real.
- Na interface: ícones Play/Pause/Stop ao final da toolbar original, sem painel
  adicional; fonte de 5 V, resistor `470R` e LED padrão resultaram em **6,07 mA**.
- Pause manteve `t = 0,820 s`; Play retomou desse ponto; Stop mostrou `t = 0 s`;
  novo Play reiniciou a sessão. Fechamento durante execução: **exit code 0**.
- Logs: `test-standard-components-session.log`, `test-standard-components-spice.log`,
  `native-toolbar-results.json`, `native-toolbar-*.png` e `gui-native-toolbar-exit.txt`.

### Resistor local, referência automática e pinos abertos

- `qa_interactive_sim`: **30 casos aprovados**, incluindo `rectifier_schlib:R/C`,
  passivos identificados por `Sim.Device`, referência automática, prioridade do GND
  explícito, duas fontes de 5 V em série produzindo 10 V, pinos abertos distintos,
  aproximadamente 5 V no terminal aberto de um resistor sem carga, capacitor com
  terminal aberto e componentes inteiramente isolados junto de um circuito ativo.
- `qa_spice`: **57 casos aprovados**. O novo caso carrega
  `open_pins_auto_reference.kicad_sch`, captura as conexões reais e confirma corrente
  de LED entre 5 e 8 mA e 5 V no pino aberto de R2.
- O exemplo foi aberto também pelo **gerenciador KiCad**, com a simulação no mesmo
  processo: Play sem erro, 6,07 mA no LED, Pause mantendo `t = 0,840 s`, retomada,
  Stop em zero e reinício. Encerramento normal: **exit code 0**.
- A localização do worker atende tanto o editor standalone quanto o gerenciador,
  na árvore de build e na instalação. A atualização do programa deve ser carregada
  reabrindo também o gerenciador KiCad: ele mantém o módulo do editor em memória.
- Evidências: `test-reference-open-pins-session.log`, `test-reference-open-pins-spice.log`,
  `reference-manager-results.json` e `reference-manager-*.png`, em `.circuitalive/logs/`.
