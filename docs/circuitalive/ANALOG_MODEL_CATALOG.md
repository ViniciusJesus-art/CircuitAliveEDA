# Modelos analógicos compactos a 25 °C

Este lote acrescenta **49 arquivos `.casim`** e **75 associações explícitas**
a símbolos da biblioteca padrão. Os parâmetros priorizam alimentação,
corrente nos pinos, níveis de tensão e tempos úteis em condições nominais de
**25 °C**. Não há varredura térmica ou reprodução de cada transistor interno.

O comportamento e as associações ficam inteiramente nos arquivos de
`simulation/models/`. Nenhuma lista adicional de componentes foi acrescentada
ao código do editor. Os modelos podem ser editados e recarregados com
**Stop → Play**, sem recompilar o programa.

## Operacionais — 15 arquivos

| Família / símbolos | Alimentação total adotada | GBW | Slew rate | Limites de corrente por saída, source / sink |
|---|---:|---:|---:|---:|
| LM358, LM358_DFN, LM324/LM324A | 3–30 V | 0,7 MHz | 0,3 V/µs | 30 / 20 mA |
| LM2904, LM2902 | 3–26 V | 0,7 MHz | 0,3 V/µs | 30 / 20 mA |
| TL071, TL072, TL074 | 10–36 V | 3 MHz | 13 V/µs | 25 / 25 mA |
| LM741 | 10–36 V | 1 MHz | 0,5 V/µs | 25 / 25 mA |
| MCP6001-OT/x-LT, MCP6001R, MCP6001U, MCP6002-xMS/xP/xSN/xMC, MCP6004 | 1,8–6 V | 1 MHz | 0,6 V/µs | 23 / 23 mA a 5,5 V; 6 / 6 mA a 1,8 V |

Cada canal tem ganho finito, polo dominante, limitação de velocidade, margem
até os trilhos e limitação independente de corrente. A corrente de saída é
retirada/entregue aos pinos de alimentação. A família MCP600x usa interpolação
simples entre os dois pontos de corrente da tabela. Nos demais modelos, os
limites de corrente são fixos. Os parâmetros podem ser alterados por instância.
São aproximações úteis, não limites certificados para qualquer carga.

Os canais de um CI duplo/quádruplo são independentes e compartilham a
alimentação do encapsulamento. Foram criados arquivos separados quando as
pinagens mudam, inclusive MCP6001R/U e encapsulamentos com pad adicional.
Pads de alimentação equivalentes são unidos por 1 mΩ; NC fica aberto.
O parâmetro `iq` representa o consumo do encapsulamento inteiro.

Use realimentação para amplificação linear. A alimentação é a diferença
entre V+ e V−: por exemplo, +12/−12 V corresponde a 24 V no modelo.
Sem alimentação válida a saída fica em alta impedância, com fuga de 1 GΩ.

O modelo não aplica a janela de modo comum como uma proteção: as entradas
devem permanecer na faixa apropriada do datasheet. Inversão de fase fora
dessa janela, todos os polos secundários, estabilidade exata com carga
capacitiva, ruído e curvas de PSRR não estão representados. Pinos NULL não
executam ajuste físico; use o parâmetro `offset`.

Referências: [LM358/LM2904](https://www.ti.com/lit/ds/symlink/lm358.pdf),
[LM324/LM2902](https://www.ti.com/lit/ds/symlink/lm324.pdf),
[TL07x](https://www.ti.com/lit/ds/symlink/tl072.pdf),
[LM741](https://www.ti.com/lit/ds/symlink/lm741.pdf) e
[MCP600x](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP6001-1R-1U-2-4-1-MHz-Low-Power-Op-Amp-DS20001733L.pdf).

## Comparadores — 5 arquivos

LM393, LM339, LM2903, LM2901 e LMV393 incluem canais independentes, offset,
corrente/capacitância de entrada, atraso de propagação e saída de coletor
aberto. **É necessário resistor de pull-up** para obter nível alto.
A saída pode drenar corrente, mas não fornece nível alto a partir de VCC.
O coletor aberto também é informado à camada de indicadores digitais.

LM393/339/2903/2901 adotam 2–36 V de alimentação e atraso nominal fixo de
1,3 µs. O LMV393 adota 2,7–5,5 V, 200 ns para HIGH→LOW e 300 ns para LOW→HIGH,
com referência às condições de 5 V e 100 mV de overdrive. O resistor/carga
externo também influencia o tempo até o nível alto.

O estágio de coletor inclui resistência, saturação aproximada, capacitância
e teto simplificado de corrente de 16 mA (15 mA no LMV393). Esses tetos não
representam todas as curvas de curto-circuito; o aviso de corrente é separado.
Não há variação do atraso com temperatura ou overdrive. Também aqui a janela
de modo comum deve ser respeitada pelo circuito.

Referências: [LM393](https://www.ti.com/lit/ds/symlink/lm393.pdf),
[LM339](https://www.ti.com/lit/ds/symlink/lm339.pdf) e
[LMV393](https://www.ti.com/lit/ds/symlink/lmv393.pdf).

## Reguladores — 21 arquivos

- AMS1117/AMS1117CD: ajustável e fixos de 1,5; 1,8; 2,5; 2,85; 3,3 e 5 V.
- LM1117DT/LM1117MP: ajustável e fixos de 1,8; 2,5; 3,3 e 5 V.
- L7905, L7912 e L7915: saídas negativas.
- L78L05, L78L09 e L78L12: TO92/SOT89 e arquivos próprios para SO8.

Há tensão de referência/saída, dropout, resposta de primeira ordem, corrente
quiescente e teto de corrente. O ajustável usa o divisor externo ligado a
ADJ. Os limites adotados são 1 A no AMS1117, 0,8 A no LM1117, 1,5 A nos L79
e 0,1 A nos L78L. São tetos simplificados próximos da capacidade nominal,
não curvas completas de limitação de curto-circuito ou dissipação contínua.

O dropout é fixo: 1,3 V no AMS1117, 1,2 V no LM1117, 2 V no L79 e 1,7 V no
L78L. A resposta adotada é de 5 µs, ajustável, como aproximação funcional.
Faltando tensão na entrada, a saída perde regulação. Não há autoaquecimento,
shutdown térmico, foldback ou análise de estabilidade versus ESR do capacitor.
As versões AMS1117CS8 e LM1117LD têm outras pinagens e não foram associadas.

Referências: [AMS1117, documento do fabricante](https://datasheet.lcsc.com/datasheet/pdf/2eb7f9cbe77f4a85a58cd1918d4199f5.pdf?productCode=C14529),
[LM1117](https://www.ti.com/lit/ds/symlink/lm1117.pdf),
[L79](https://www.st.com/resource/en/datasheet/l79.pdf) e
[L78L](https://www.st.com/resource/en/datasheet/l78l.pdf).

## Optoacopladores — 4 arquivos

PC817, EL817, LTV-817/M/S e TLP291 usam LED, corrente óptica controlada por CTR,
resposta de primeira ordem e fototransistor com saturação e teto de 50 mA.
CTR padrão é 100% e pode ser ajustado conforme a classe do componente;
não significa que todos os exemplares tenham esse CTR.
Os domínios de entrada e saída são eletricamente separados.

As medições V/I/P do componente referem-se ao LED por padrão. A corrente do
coletor pode ser medida no fio de saída. Não são reproduzidos breakdown de
isolação, operação reversa do fototransistor nem todas as caudas de carga.
O TLP291-4 não está incluído: é um encapsulamento com quatro canais.

Referências: [PC817](https://global.sharp/products/device/lineup/data/pdf/datasheet/PC817XxNSZ1B_e.pdf),
[EL817](https://www.everlight.com/wp-content/plugins/ItemRelationship/product_files/pdf/EL817-G.pdf),
[LTV-817](https://optoelectronics.liteon.com/upload/download/DS-70-96-0016/LTV-8X7%20series%20%20Rev.S.PDF) e
[TLP291](https://toshiba.semicon-storage.com/info/docget.jsp?did=12884&prodName=TLP291).

## Potenciômetros — 4 arquivos

Potenciômetros simples/duplos e versões com pino de montagem usam o campo
**Value** como resistência total, por exemplo `10k`. O parâmetro **position**
vai de 0 a 1: 0 liga o cursor à extremidade inferior (pino 3); 1, à superior
(pino 1). Trilhas duplas compartilham posição, mantendo isolamento elétrico.
O pino MP não tem conexão elétrica ao cursor.

A curva é linear. Durante a simulação, clique no corpo do potenciômetro ou
trimpot para abrir o controle de posição ao vivo. A posição inicial continua
configurável em **Simulation model...**. Veja os [ajustes ao vivo](LIVE_CONTROLS.md).
Não há ruído/desgaste mecânico.

## Execução e verificação

São redes pequenas de elementos SPICE e, nos comparadores, atrasos XSPICE.
Não executam scripts de componente no timer da interface. Operacionais e
optoacopladores solicitam Gear de ordem 2 para amortecer oscilações numéricas;
a opção afeta o circuito inteiro. Não impõem um passo máximo global de ns.
A resolução necessária continua dependendo das frequências do circuito.

As bancadas de `qa/tests/spice/test_analog_model_catalog.cpp` verificam todos
os 49 arquivos, incluindo canais físicos independentes, sinais negativos,
realimentação, ganho em frequência, slew rate, correntes source/sink,
MCP600x a 1,8/5,5 V, saída sem alimentação, atraso/coletor aberto,
regulação/dropout/sobrecarga, divisor de ADJ, CTR e isolamento dos optos,
posição e independência das trilhas dos potenciômetros.
A auditoria de pinagem também confere as 75 associações da biblioteca.
