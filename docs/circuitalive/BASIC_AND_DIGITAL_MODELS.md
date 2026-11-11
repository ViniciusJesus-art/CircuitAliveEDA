# Componentes básicos e simulação digital

Os comportamentos continuam em arquivos `.casim` carregados no Play. Os símbolos
vêm das bibliotecas padrão; os novos modelos são didáticos e configuráveis,
sem a pretensão de reproduzir um lote ou um macromodelo certificado do fabricante.

## Modelos disponíveis

O catálogo foi ampliado com [15 modelos de componentes discretos](DISCRETE_MODELS.md),
incluindo TIP122/TIP127, TIP41C/TIP42C, BD139/BD140, MOSFETs, Zeners e TIC106.

Além de R, C, fonte DC, LED e chave, foram acrescentados:

| Símbolo | Modelo e configuração |
|---|---|
| `Simulation_SPICE:VSIN` | Fonte senoidal: offset, amplitude de pico, frequência, atraso e fase |
| `Simulation_SPICE:VPULSE` | Fonte pulsada: níveis baixo/alto, atraso, subida, descida, largura e período |
| `Device:L`, `L_Small` | Indutor ideal, valor do campo Value, corrente inicial zero |
| `Device:D`, `D_Small` | Diodo de silício genérico |
| `Diode:1N4148`, `1N4148W` | Diodo de comutação |
| `Diode:1N4001` até `1N4007` | Retificadores, com arquivos separados e tensão de ruptura configurável |
| `Diode:1N5819` | Diodo Schottky |
| `Transistor_BJT:BC327`, `BC337`, `BC547`, `BC548`, `BC557`, `BC558` | Transistores PNP/NPN, com ganho, corrente de saturação, efeito Early e capacitâncias configuráveis |
| `74xx:74HC00` | Quatro portas NAND, alimentação compartilhada e atrasos |
| `74xx:74HC125` | Quatro buffers com saída de três estados e /OE ativo em nível baixo |

Abra **Simulation model…** com Ctrl+Alt+M para configurar. A fonte AC usa
inicialmente 5 V **de pico** e 50 Hz; não é um valor RMS. A pulsada começa
em 0 V e alterna para 5 V. Os campos `Sim.*` nativos desses novos modelos não
substituem os parâmetros `.casim`; configure-os nessa janela. R/C/L acompanham
Value. Acrescentar ou alterar esses arquivos não exige recompilar.

A fonte senoidal limita o passo a, no máximo, 1/100 do período, ou 100 µs,
o menor dos dois. O horizonte finito é reduzido para manter o limite de passos
do worker quando necessário. Frequências altas podem tornar o avanço mais lento.

Os diodos usam modelos SPICE D e os transistores, Q. As pinagens são as dos
símbolos: diodo 1=K/2=A; os BC incluídos usam 1=C/2=B/3=E. O ganho nominal é
editável; o modelo não implementa dano térmico nem todos os limites de operação.
Para comparação com um componente físico, use o datasheet e um modelo validado
para o fabricante e a aplicação. Por exemplo, a pinagem e as condições do BC327
estão no [datasheet da onsemi](https://www.onsemi.com/download/data-sheet/pdf/bc327-d.pdf).

Para testar bloqueio reverso, use uma carga com retorno ao GND. Um terminal
ligado somente a uma sonda pode mostrar quase a tensão da fonte, com corrente
mínima. Veja a [bancada de bloqueio reverso dos diodos](DIODE_REVERSE_BIAS.md).

## Painel de probes recolhido

O painel lateral inicia **oculto**. A seta diagonal no final da barra direita
abre as ferramentas de probes. Escolha uma ferramenta e clique no fio.
Clicar novamente na seta cancela a colocação, retorna ao cursor e esconde o
painel. Esc termina apenas a colocação.

Durante a simulação, o painel abre automaticamente se houver medições ou probes
de osciloscópio. Ele também pode ser recolhido pelo botão; não reaparece a cada
atualização da tela. Stop recolhe os resultados, mantendo as ferramentas abertas
somente se o botão de probes estiver selecionado.

## Níveis nos pinos

O novo botão **Pin logic levels**, imediatamente após Stop, liga e desliga as
indicações no esquema, sem reiniciar o solver:

| Cor | Estado |
|---|---|
| Vermelho | 1 |
| Azul | 0 |
| Cinza | Z, sem acionamento |
| Amarelo | X, indefinido ou fora de uma condição lógica válida |
| Rosa, fio espesso e `!` nos pinos | Disputa entre saídas ativas no barramento |

Para os CIs digitais, os limiares são relativos aos **próprios pinos VCC e GND**.
Os padrões didáticos HC são 0 até 0,3 × VCC e 1 a partir de 0,7 × VCC, sempre
subtraindo GND. A faixa de alimentação, os limiares, a resistência de saída,
a capacitância de entrada e os atrasos são parâmetros editáveis. Alimentação
fora da faixa gera aviso no rodapé e desabilita as saídas neste modelo.
Os valores nominais não reproduzem toda a dependência com temperatura, tensão
e carga; consulte o [datasheet do 74HC00/74HCT00](https://assets.nexperia.com/documents/data-sheet/74HC_HCT00.pdf).

Os novos 74HC14, 74LS14 e CD40106 preservam o estado entre os limiares de
histerese. LS00/LS14 usam limiares absolutos em relação ao GND local e correntes
de entrada TTL. HC00/HC14 incluem consumo interno por frequência. Esses cinco
modelos também têm resistências distintas e avisos de corrente por saída.
Veja [calibração elétrica e limites](SCHMITT_ELECTRICAL_MODELS.md); os demais
modelos digitais ainda não receberam todos esses recursos.

As unidades A–E do mesmo U1 são agrupadas em um único encapsulamento. Coloque
também a unidade de alimentação e conecte VCC/GND. Portas não colocadas ficam
com os pinos ausentes abertos, sem impedir a simulação das demais. Configurar
o modelo por uma unidade atualiza os campos de todas as unidades daquele CI,
em uma única operação de desfazer.

Z não é deduzido de uma tensão intermediária. O arquivo declara quando cada
saída está habilitada, mas os indicadores dos pinos mostram o **nível lido na
conexão**, separado do estado do acionamento local. Com /OE=1 no 74HC125 ou
na 27C512, o estágio de saída fica em Z; se a RAM ou um pull-up mantiver o
barramento em 1, o pino desabilitado também mostra 1. O mesmo vale para 0.
Sem fonte nem polarização resistiva, a conexão mostra Z; uma tensão entre os
limiares é X. Uma entrada indefinida pode tornar a saída X;
por exemplo, NAND(X,1)=X e NAND(X,0)=1. A alimentação local e os limiares de
cada família continuam sendo considerados; um CI sem alimentação válida
não tem condições válidas para interpretar o nível externo.

Redes de resistores propagam a presença de acionamento, mas mantêm suas
resistências individuais no cálculo elétrico. Um resistor entre dois nós
flutuantes ou um capacitor para GND não inventa uma fonte lógica. Sensores
ideais de corrente e potência preservam a continuidade do barramento.

Quando duas saídas ativas tentam impor níveis opostos e há corrente circulando
entre elas, o fio inteiro fica rosa e mais espesso. O rodapé identifica os
pinos em disputa. O aviso é removido quando a disputa termina e **não pausa a
simulação**. Resistores de pull-up/pull-down, entradas e pull-ups fracos de
GPIO não são contados como saídas em disputa. Saídas de coletor aberto podem
compartilhar o mesmo pull-up sem falso alarme. A tensão resultante, as quedas
e as correntes continuam sendo calculadas pelo ngspice, com os parâmetros dos
modelos; não são substituídas por uma prioridade artificial de 0 ou 1.

A detecção acompanha portas de saída declaradas pelos modelos, incluindo
probes de estímulo, fontes, memórias, lógica e GPIOs. Ela não analisa
universalmente o acionamento interno de qualquer circuito analógico ou
subcircuito personalizado. O diagnóstico visual usa a amostra elétrica atual;
pulsos de disputa menores que o intervalo de atualização podem exigir análise
no osciloscópio. Avisos de sobrecorrente continuam independentes, conforme os
limites implementados por cada modelo.

O comportamento de liberação por /OE está descrito no
[datasheet da AT27C512R](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/DataSheets/doc0015.pdf).
Para o efeito das características elétricas das saídas durante disputas, veja
[Designing With Logic, da Texas Instruments](https://www.ti.com/lit/an/sdya009c/sdya009c.pdf).

Circuitos analógicos conectados a uma entrada são avaliados pela tensão que
produzem; a implementação conserva uma classificação de driver possível
nesses casos e não faz uma análise universal de alta impedância de qualquer
rede analógica. Em pinos de componentes sem perfil lógico, as cores são apenas
uma leitura de tensão com limiares de 1,5/3,5 V em relação à referência da ilha
(perfil padrão de 5 V), sem afirmar que o componente possui lógica digital.

## Como o comportamento digital é executado

Os arquivos atuais usam o backend `ngspice` com portas **XSPICE** e conversores
analógico/digital. Os eventos e atrasos acontecem dentro do solver; o timer da
interface só apresenta o resultado. Atrasos de nanossegundos foram verificados
nos testes, independentemente do intervalo de atualização da tela. O passo
máximo analógico de 100 µs não garante capturar qualquer pulso analógico mais
curto: configure o estímulo e a resolução conforme a análise necessária.

A saída tem condutâncias controladas para VCC e GND, com resistência finita.
A corrente de uma carga sai da alimentação do componente, permitindo medi-la
na porta VCC/GND. O estado X usa acionamento fraco no ponto médio; isso é uma
convenção numérica do modelo didático. Proteções ESD, back-powering, destruição,
metastabilidade física e consumo interno completo ainda não são reproduzidos.

Uma nova função lógica pode ser criada em outro arquivo usando os elementos
XSPICE disponíveis e associada a outro desenho. Não existe uma lista de CIs
permitidos em C++. Isso não define uma linguagem própria para microcontroladores
ou firmware. O formato de uma linguagem desse tipo permanece uma decisão futura.
Referência dos conversores, portas e atrasos:
[manual oficial do ngspice, modelos XSPICE](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).

## Campos adicionais do arquivo

O bloco `spice` continua definindo o circuito. O bloco opcional `logic` declara
a alimentação e os sinais usados para interpretar os pinos:

```json
"logic": {
  "vcc": "vcc", "gnd": "gnd",
  "low_parameter": "vil", "high_parameter": "vih",
  "min_supply_parameter": "vmin", "max_supply_parameter": "vmax",
  "driver_enable": {"y1": "v(en_${id})"},
  "driver_value": {"y1": "v(code1_${id})"}
}
```

Os parâmetros referenciados precisam existir em `parameters`; as portas,
em `pins`. `driver_enable` é um sinal analógico salvo: 0 desabilita e valores
acima de 0,5 habilitam. `driver_value` opcional é o estado normalizado 0/1;
um valor entre os limiares conserva X mesmo que outra carga altere a tensão.
O modelo deve implementar eletricamente o mesmo enable que declara.

`${drive:porta}` nas linhas SPICE é expandido para a presença de um driver
possível na rede. Ele permite que o conversor de uma entrada aberta produza
indefinido, em vez de interpretar a estabilização numérica por `rshunt` como 0.
Portas com role `input` não contam como drivers de sua própria rede, inclusive
em modelos analógicos sem a seção `logic`. Estes também podem usar o placeholder
para impedir seleção indevida por controles abertos.
Probes A/W preservam essa informação através de seus sensores ideais em série.
`frequency_parameter` opcional aponta para o parâmetro que limita o passo da
fonte senoidal. Veja os arquivos completos `74hc00.casim`, `74hc125.casim` e
`ac_voltage.casim` em `simulation/models`.

## Exemplos e validação

```bash
./circuitalive eeschema demos/simulation/circuitalive/digital_74hc00.kicad_sch
./circuitalive eeschema demos/simulation/circuitalive/analog_rectifier.kicad_sch
```

No digital, abra SW1/SW2 para elevar A/B pelos pull-ups e observe a NAND.
A entrada FLOAT mostra Z; a saída correspondente mostra X. No analógico,
Measurements mostra o diodo e o capacitor; Oscilloscope acompanha a retificação.

O código de indicação fica em `logic_signals.*`, separado da leitura de símbolos,
do runner e do desenho. A sessão salva até 256 sinais e limita o total do
histórico a 65.536 valores; não conserva todo o histórico de eventos XSPICE.

Validação atual: 54 testes interativos e 63 testes SPICE. Inclui seno/fase,
retificação, NPN/PNP, carga do indutor, NAND, alimentação deslocada, Z/X,
tri-state com pull-up, atraso de 20 ns, corrente de alimentação e agrupamento
de unidades de arquivos nativos. A interface foi validada em Xvfb isolado.
