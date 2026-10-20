# Arquivos de modelo e configuração por componente

Para reconhecer cópias de símbolos, nomes de peças e pinagens diferentes da mesma
família, veja [Associações automáticas e demos](MODEL_ASSOCIATIONS.md).

Esta etapa substitui o catálogo de componentes em C++ por **arquivos carregados
em tempo de execução**. O símbolo descreve o desenho e os pinos físicos; o
arquivo descreve o comportamento elétrico, os parâmetros e os terminais do modelo.
O programa conhece o backend ngspice e os indicadores/interações disponíveis,
sem uma lista de componentes permitidos no executável.

O envelope `.casim` v1 atende modelos analógicos e digitais XSPICE no backend
ngspice. O comportamento fica no arquivo, incluindo portas e atrasos. A seção `behavioral` permite compor portas, flip-flops, latch e RAM com interfaces
elétricas. Veja [Perfis e componentes complexos](COMPLEX_MODELS.md).
Veja [Componentes básicos e digital](BASIC_AND_DIGITAL_MODELS.md).

## Configurar no editor

Para criar o arquivo pela interface, abra **Simulation Model Editor** na janela
principal. O [editor visual de modelos](MODEL_AUTHOR.md) permite escolher um
símbolo, conectar blocos e configurar parâmetros sem escrever o JSON abaixo.

1. Selecione um símbolo, clique com o botão direito e escolha **Simulation model...**.
   O atalho é **Ctrl+Alt+M**; sobre um símbolo, ele também permite selecioná-lo.
2. Escolha um dos arquivos disponíveis ou use **Load file...** para um arquivo próprio.
3. Associe cada terminal do modelo ao número do pino físico do símbolo.
   A coluna Role descreve entrada, saída, alimentação, referência ou terminal passivo.
4. Edite os parâmetros desejados. Campo vazio usa o padrão do arquivo;
   `$value` acompanha o campo Value do símbolo. São aceitas unidades como `3.2V`,
   `20mA`, `470R` e `100uF`.
5. Confirme e salve o esquema. Play cria uma sessão usando os arquivos atuais.

A associação e os parâmetros ficam em campos ocultos do próprio símbolo:

| Campo | Conteúdo |
|---|---|
| `CA.Model` | `builtin:<id>` para modelos distribuídos ou caminho de um arquivo próprio |
| `CA.Pins` | Objeto JSON `terminal do modelo → número físico do símbolo` |
| `CA.Parameters` | Objeto JSON de substituições por componente |
| `CA.Measurements` | Seleção V/I/P e par de terminais lógicos para medir |

Essas alterações usam o histórico de desfazer do KiCad. Cancelar a janela não
altera o esquema. A confirmação invalida a sessão anterior como qualquer edição.
Cada instância conserva seus próprios parâmetros; modificar um LED não modifica
os outros já colocados. A opção **Use these settings for new … symbols** guarda
um perfil por ID de biblioteca para os próximos componentes, conforme
[Perfis e componentes complexos](COMPLEX_MODELS.md). Arquivos externos ao projeto usam caminho absoluto; arquivos dentro
do projeto usam caminho relativo à pasta do projeto. Mantenha esses arquivos
junto do projeto ao compartilhá-lo.

**Não é necessário recompilar para criar, associar ou editar um modelo.**
Uma nova sessão, iniciada após Stop, lê novamente os arquivos. Play durante Pause
retoma a cópia já carregada. **Reload file** atualiza a descrição na janela de
configuração. Uma alteração somente no arquivo não modifica uma sessão em curso.

### Interações visuais

O [editor de modelos](MODEL_AUTHOR.md#controles-visuais-durante-a-simulação) permite
configurar botões e sliders sem escrever SPICE ou JSON. O arquivo exportado usa
`controls` para botões independentes e `adjustments` para ajustes contínuos.
Os controles de blocos internos são associados a fontes V/I com nomes próprios
por bloco e por instância. O formato antigo `control` continua aceito.

Cada controle pode conter `visual: {"target":"symbol"}` para usar o símbolo
inteiro, ou `visual: {"target":"rectangle","x":1.2,"y":0,"width":0.5,"height":0.3}`
para uma área posicionada. As coordenadas são frações do corpo exibido do símbolo,
com origem no canto superior esquerdo. As mesmas áreas são usadas para desenho
e clique, sempre filtradas pela folha atual.
O campo opcional `visual.visible`, com padrão `true`, permite ocultar borda e
texto com `false`; ele não altera a área de clique. A configuração vale tanto
para o alvo `symbol` quanto para `rectangle`.

A seção `authoring.interactions` conserva o bloco, o controle, o rótulo e o
posicionamento editáveis. Os estados iniciais dos botões e os parâmetros iniciais
dos sliders são aplicados ao circuito gerado antes do primeiro passo; os cliques
seguintes alteram a fonte da instância ativa, sem reconstruir o circuito.

Referências de valores `#name` ficam preservadas em `authoring.blocks[].values`.
O exportador valida os tipos e os limites e gera `live_updates` para parâmetros
elétricos mutáveis, ou fontes de controle internas para expressões comportamentais.
Alterações são enviadas junto com o evento do controle, no mesmo instante pausado
do solver, sem reset da análise. Os nomes dos dispositivos são exclusivos por
bloco e por instância. `live_texts` mantém referências de Label/Display unit fora
do deck elétrico; texto de apresentação nunca é inserido como comando SPICE.

Modelos visuais guardam o diagrama e seus blocos incorporados na seção
`authoring`. O simulador usa a seção elétrica já gerada. A seção opcional
`logic.internal_drive` associa portas externas a sinais internos de presença
de acionamento, preservando pull-ups e outros caminhos analógicos de modelos
compostos na classificação de pinos digitais. Como em `driver_enable`, cada
entrada identifica um sinal de tensão salvável, como `v(no_interno_${id})`.

## Localização e associação automática

Os arquivos `.casim` são procurados diretamente nestas pastas, nesta ordem:

1. `models/` dentro da pasta do projeto.
2. Pasta indicada por `CIRCUITALIVE_MODEL_DIR`, quando configurada.
3. `share/circuitalive/models` na instalação.
4. `simulation/models` no checkout, para desenvolvimento.

O mesmo ID na primeira pasta tem precedência. A busca não é recursiva.
O projeto pode manter uma cópia de um modelo para fixar seus parâmetros.
O launcher deste checkout define `CIRCUITALIVE_MODEL_DIR` como `simulation/models`
quando a variável não foi definida, permitindo editar esses arquivos e usar a
instalação local diretamente, sem recompilar nem reinstalar.

Uma associação explícita no símbolo tem prioridade. Sem ela, `matches` dentro
dos próprios arquivos resolve, por prioridade: ID completo de biblioteca,
nome curto do símbolo e indicação `Sim.Device`. Duas associações de mesma
prioridade produzem erro e pedem uma escolha explícita. Um arquivo sem `matches`
pode ser associado manualmente a qualquer desenho cuja pinagem seja compatível.
Uma associação inexistente ou arquivo inválido é identificado antes da simulação.

Os primeiros arquivos distribuídos são `resistor.casim`, `capacitor.casim`,
`dc_voltage.casim`, `led.casim` e `switch.casim`. São os arquivos que declaram
as associações dos componentes padrão e das cópias locais de R/C. O catálogo
atual inclui também [componentes básicos e digitais](BASIC_AND_DIGITAL_MODELS.md). Para manter a
compatibilidade anterior, os modelos ideais R/C desconsideram campos SPICE antigos;
nos demais, campos SPICE personalizados pedem uma associação explícita.

## Exemplo de arquivo analógico

Um resistor próprio pode ser descrito inteiramente neste arquivo:

```json
{
  "schema_version": 1,
  "backend": "ngspice",
  "id": "my_resistor",
  "name": "My resistor",
  "pins": [
    {"id": "p", "label": "Terminal 1", "role": "passive", "default_pin": "1"},
    {"id": "n", "label": "Terminal 2", "role": "passive", "default_pin": "2"}
  ],
  "parameters": [
    {"id": "resistance", "label": "Resistance", "unit": "Ohm",
     "default": "$value", "min": 0.000001, "max": 1000000000000}
  ],
  "spice": {
    "instance": ["R${id} ${pin:p} ${pin:n} ${param:resistance}"]
  }
}
```

O gerador substitui `${id}` por um identificador da instância, `${pin:p}` pelo
nó elétrico do terminal mapeado e `${param:resistance}` pelo número validado.
O texto do Value não é inserido como código SPICE. Terminais e parâmetros
desconhecidos, pinagem repetida ou incompleta, valores não numéricos e parâmetros
fora dos limites são recusados.

`spice.definitions` aceita linhas de `.model`, `.subckt` e outros elementos do
deck suportados pelo worker. Nomes locais a uma instância devem incluir `${id}`
para evitar conflitos. Subcircuitos podem ter mais de dois terminais; veja
`demos/simulation/circuitalive/models/voltage_divider.casim`. A ordem dos terminais
na linha X é a ordem da declaração `.subckt`. O número do pino físico do símbolo
não precisa coincidir com a posição do terminal no modelo.

Os arquivos contêm o modelo inline. Nesta versão não são carregados scripts do
sistema, bibliotecas nativas, comandos `.control` nem includes SPICE externos.
O worker continua responsável pelas análises, pausa, retomada e tempo de execução.

Pinos abertos continuam sendo nós distintos. `reference_pin` identifica um
terminal que pode fornecer a referência automática, como o negativo da fonte.
`electrical_groups` permite separar grupos galvanicamente isolados de um mesmo
modelo; na ausência desse campo, todos os seus terminais pertencem ao mesmo grupo
para escolher a referência numérica. GND explícito continua tendo prioridade.

## Medições e avisos no arquivo de modelo

O campo opcional `measurement_ports` escolhe o par padrão da janela V/I/P:

```json
"measurement_ports": {"positive":"p", "negative":"n"}
```

Os nomes são IDs lógicos declarados em `pins`. Modelos de dois terminais sem
esse campo usam o primeiro e o último terminal. Modelos com mais terminais
precisam de um par explícito no arquivo ou na seleção por componente.
I é a corrente que entra no terminal positivo; P usa a tensão entre o par
multiplicada por essa corrente. Veja [Medições e osciloscópio](MEASUREMENTS.md).

Um monitor opcional declara um sinal de corrente e um parâmetro de limite:

```json
"current_warning": {"signal":"i(V${id})", "limit_parameter":"current_warning"},
"voltage_constraint_parameter": "voltage"
```

`limit_parameter` precisa existir em `parameters`; valores positivos habilitam
o aviso e 0 o desativa. O sinal usa os mesmos templates do netlist. O campo
`voltage_constraint_parameter` é reservado a uma diferença de potencial ideal
imposta entre os `measurement_ports`: se o parâmetro for diferente de zero,
terminais na mesma rede geram erro de curto antes de iniciar a sessão.
A fonte DC distribuída declara ambos; o primeiro não adiciona resistência
interna nem limita corrente.

## LED

O arquivo `led.casim` expõe:

| Parâmetro | Efeito |
|---|---|
| Forward voltage at reference current | Tensão direta do modelo na corrente de referência, a 27 °C |
| Reference current | Corrente em que a tensão acima é especificada |
| Emission coefficient / Series resistance | Forma da curva elétrica do diodo |
| Current for full displayed brightness | Escala do indicador visual |
| Current warning threshold | Acrescenta `overcurrent` ao status quando ultrapassado |

A corrente real resulta do circuito. A corrente de referência não força uma
corrente no LED, e o limite de aviso não limita a corrente nem simula destruição.
O indicador atual conserva a cor inicial; cores e outros indicadores poderão
ser descritos por extensões visuais. Temperatura e efeitos ópticos avançados
também não fazem parte deste modelo didático.

O arquivo normaliza a corrente de saturação usando a área do diodo e compensa
a resistência de série pela mesma escala. Assim a calibração também funciona
com tensões diretas maiores, sem depender de valores de IS abaixo do limite
numérico observado no ngspice 47. As regras de escala por área e o modelo de
diodo estão no [manual oficial do ngspice, capítulo 7.2](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).

## Contrato digital a discutir

Para um 74HC00 programável, a associação precisará separar:

- **Encapsulamento/pinagem:** números físicos, quatro portas, VCC/GND e unidades do símbolo.
- **Comportamento lógico:** funções, estado interno e eventos com tempos de propagação.
- **Perfil elétrico:** alimentação, limiares de entrada, impedância/corrente das saídas e carga.
- **Resolução de rede:** estado de cada saída e estado resultante do barramento, incluindo 0/1/Z/X.
- **Visualização:** estado do driver, estado lido nas entradas e indicação no desenho.

Z pertence ao estado de acionamento de uma saída. Um resistor de pull-up pode
levar a rede a 1 enquanto a saída continua Z. Uma tensão intermediária não é uma
prova de alta impedância. Entradas e saídas de uma família lógica precisam
considerar sua alimentação; atrasos precisam ser representados no tempo do motor.

Os modelos atuais 74HC00 e 74HC125 já usam eventos XSPICE, agrupam as unidades
do mesmo CI e exibem 0/1/Z/X opcionalmente nos pinos. Declarar `role: output`
sozinho continua insuficiente: o circuito SPICE e o bloco `logic` do arquivo
definem comportamento, alimentação e enable. Uma linguagem digital própria
ainda não foi definida. Detalhes em [Componentes básicos e digital](BASIC_AND_DIGITAL_MODELS.md).

## Organização

- `simulation_model.*`: leitura, descoberta, associação, parâmetros e expansão de templates, sem wx/KiCad.
- `model_dialog.*`: escolha de arquivo, pinagem e parâmetros; edita uma cópia.
- `schematic_adapter.*`: leitura/gravação dos campos no símbolo e captura das conexões.
- `circuit_topology.*`: nós, grupos elétricos e referências, usando a pinagem do modelo.
- `circuit_snapshot.*`: congela os modelos em um deck para o worker.
- `simulation/models/*.casim`: comportamentos e associações iniciais, sem recompilação.

Somente modificar o carregador, um backend ou os recursos da interface exige
recompilar o EDA. Acrescentar componentes expressáveis pelo backend existente
exige somente novos arquivos e suas associações.

## Verificação desta etapa

- `qa_interactive_sim`: 36 casos aprovados, incluindo arquivo externo com três
  pinos, subcircuito, alterações no arquivo, associação automática e ambiguidade,
  validação de pinagem/parâmetros, configuração independente de LEDs e calibração
  da tensão direta de 1,2 V a 6 V.
- `qa_spice`: 58 casos aprovados, incluindo captura de esquema com associação
  explícita a um desenho arbitrário e persistência dos campos de configuração.
- Interface validada em Xvfb pelo gerenciador KiCad: parâmetros do LED alteraram
  a corrente de 6,07 mA para 4,17 mA; o limite de 1 mA gerou aviso de sobrecorrente.
  Desfazer restaurou 6,07 mA e refazer recuperou a configuração. Um arquivo próprio
  foi carregado e salvo como caminho relativo `models/test_led.casim`.
- Evidências locais em `.circuitalive/logs/`: `test-model-files-session.log`,
  `test-model-files-spice.log`, `model-files-ui-results.json` e `model-files-*.png`.

## Seleção e ajustes ao vivo

O [navegador de modelos e os controles ao vivo](LIVE_CONTROLS.md) documentam
os campos opcionais `category`, `family` e `adjustments`. Potenciômetros e
trimpots podem mudar de posição durante a execução, sem reiniciar o circuito.

Imagens por instância e regiões de memória são descritas em [MEMORY_IMAGES.md](MEMORY_IMAGES.md). O campo oculto `CA.Resources` guarda as escolhas de cada componente.
