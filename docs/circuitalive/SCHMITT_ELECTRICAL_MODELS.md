# Portas Schmitt e parâmetros elétricos digitais

Esta etapa acrescenta três arquivos: `74hc14`, `74ls14` e `cd40106`.
Há quatro associações na biblioteca padrão: `74xx:74HC14`, `74xx:74LS14`,
`4xxx:40106` e `4xxx_IEEE:40106`. Cada encapsulamento preserva os seis
inversores independentes, com alimentação em 14/7. Os modelos existentes
`74ls00` e `74hc00` também recebem calibração elétrica.

A temperatura fica fora do escopo. A prioridade desta etapa é representar
carga nas entradas, fan-out, queda de tensão nas saídas, alimentação, consumo,
atraso e resposta à carga capacitiva. Os parâmetros abaixo são pontos de
referência; isso ainda não equivale a reproduzir todas as curvas do componente.

## Comportamento Schmitt

Cada entrada tem limiares separados para subida e descida. Entre eles,
conserva o último estado válido; uma partida dentro dessa faixa começa em X.
Perda de alimentação ou de acionamento externo invalida esse estado.
As entradas dos seis inversores têm memórias independentes.

| Modelo | Alimentação recomendada representada | Referência dos limiares | VT− / VT+ |
|---|---|---|---|
| 74HC14 | 2–6 V | Razões calibradas em 4,5 V | 1,40 / 2,38 V a 4,5 V |
| 74LS14 | 4,75–5,25 V | Valores absolutos | 0,80 / 1,60 V |
| CD40106B | 3–18 V | Razões calibradas em 5 V | 1,90 / 2,90 V a 5 V |

Os valores vêm das tabelas de transferência do
[74HC14 Nexperia](https://assets.nexperia.com/documents/data-sheet/74HC_HCT14.pdf),
do [74LS14 TI](https://www.ti.com/lit/ds/symlink/sn74ls14.pdf) e do
[CD40106B TI](https://www.ti.com/lit/ds/symlink/cd40106b.pdf).
As razões CMOS aproximam outros valores de alimentação; não interpolam todas
as curvas. No CD40106, os limiares típicos independentes dão diferença de 1 V;
a tabela também caracteriza a histerese típica separadamente como 0,9 V.

## Carga, fan-out e consumo

O 74LS00 agora mantém seus limiares absolutos de 0,8/2 V em toda a faixa
recomendada, inclusive nos indicadores e monitores de atividade do editor.
Nos modelos comportamentais, a entrada sem driver usa o centro da própria
faixa indefinida, evitando que o antigo valor fixo 0,5 fosse interpretado
como HIGH por uma entrada TTL.

Para LS00/LS14, a corrente de entrada interpola −0,4 mA em 0,4 V e +20 µA
em 2,7 V, conservando esses valores além dos pontos de referência. A corrente
negativa em nível baixo precisa ser absorvida pelo circuito que aciona a entrada.
Esses são valores conservadores para calcular fan-out; o modelo não representa
os transistores compartilhados por entradas de uma mesma porta. A corrente de
repouso do encapsulamento usa valores típicos e é modelada separadamente.

Cada saída tem resistências distintas para fornecer e absorver corrente.
O estágio interpola uma única tensão alvo durante a transição; a corrente
fornecida à carga passa por VCC e a corrente absorvida passa por GND.
Isso evita contar condução simultânea artificial dos drivers além do consumo
interno já calibrado. Correntes inversas, clamps e proteção não estão calibrados.

| Modelo | R HIGH / LOW | Referência de corrente por saída | Consumo interno de repouso |
|---|---|---|---|
| 74LS00 | 150 / 18,75 Ω | 0,4 / 8 mA | 0,8 mA HIGH; 2,4 mA LOW |
| 74LS14 | 150 / 18,75 Ω | 0,4 / 8 mA | 8,6 mA HIGH; 12 mA LOW |
| 74HC00 | 45 / 37,5 Ω | 4 / 4 mA em 4,5 V | 1 µA nominal ilustrativo |
| 74HC14 | 45 / 37,5 Ω | 4 / 4 mA em 4,5 V | 1 µA nominal ilustrativo |
| CD40106B | 400 / 400 Ω | 0,51 / 0,51 mA em 5 V | 20 nA típico em 5 V |

Para LS, a saída sem carga fica em VCC−1,6 V ou 0,2 V. O consumo de repouso
interpola os dois valores pela média dos estados das saídas. As correntes de
entrada LS e os limites de saída permitem verificar fan-out nominal de 20
entradas: a 21ª já excede a corrente de referência. A tensão não muda
arbitrariamente para X por contagem de pinos: ela é calculada pelo circuito.
Referências: [SN74LS00 TI](https://www.ti.com/lit/ds/symlink/sn74ls00.pdf) e
[SN74LS14 TI](https://www.ti.com/lit/ds/symlink/sn74ls14.pdf).

Nos HC, as resistências ajustam os pontos típicos de 4,32 V e 0,15 V com 4 mA
em 4,5 V. No CD40106, ajustam queda típica de 0,4 V a 1 mA em 5 V.
As correntes da tabela geram avisos separados por saída e sentido.
**São referências de operação/teste; não são clamps de corrente, limites de
curto-circuito nem limites universais em qualquer alimentação.** O valor
0,51 mA do CD40106 é um ponto de drive garantido, não um máximo absoluto.
Os avisos usam um filtro de primeira ordem de 1 µs, configurável, para que
uma borda capacitiva normal não deixe uma sobrecarga permanente no editor.
Corrente média elevada em comutação repetitiva continua gerando aviso; a janela
é uma política de medição do modelo, não uma constante do datasheet.

HC00 e HC14 incluem consumo interno por entrada que comuta, com capacitâncias
CPD de 22 pF e 7 pF, respectivamente. A média segue `ICC = CPD × VCC × f`
por entrada. Isso também ocorre com uma entrada de NAND mascarada pela outra.
A carga externa acrescenta sua energia pelo estágio de saída, separadamente.
Um RC interno calcula a carga das transições sem amplificar oscilações numéricas
de derivadas. Consulte a equação e a definição de N nos datasheets
[74HC00](https://assets.nexperia.com/documents/data-sheet/74HC_HCT00.pdf) e
[74HC14](https://assets.nexperia.com/documents/data-sheet/74HC_HCT14.pdf).

## Tempo e frequência

Os atrasos internos nominais de subida/descida são 9/10 ns no LS00,
7/7 ns no HC00, 15/15 ns no LS14, 11/11 ns no HC14 e 120/120 ns no CD40106.
Somam-se a transição do driver e a resposta da resistência/capacitância externa.
Esses atrasos internos não devem ser confundidos com o tempo total de um
ensaio de datasheet. As bancadas usam 15 pF em HC14/LS14 e 50 pF no CD40106
para conferir a ordem e faixa dos atrasos carregados.

O aumento de frequência encontra os atrasos e a largura de banda efetiva
sob carga. Não se inventa uma frequência máxima fixa para portas combinacionais.
As curvas de atraso versus VCC, limites de pulsos e setup/hold dos sequenciais
continuam pendentes de calibração. Consumo interno dinâmico dos TTL e CD40106,
excesso de corrente em rampas CMOS lentas, proteção, alimentação parasitária e
falhas por sobrecarga também não estão representados nesta etapa.

## Formato e verificação

A seção opcional `behavioral.input_hysteresis: true` ativa a memória dos
limiares em todas as entradas. `behavioral.electrical` associa recursos a IDs
de parâmetros existentes no arquivo:

| Chave | Efeito |
|---|---|
| `threshold_reference` | Referência fixa em volts para normalizar vil/vih; ausência acompanha VCC |
| `input_low_current`, `input_high_current` | Magnitudes das correntes DC de entrada |
| `input_low_reference`, `input_high_reference` | Tensões dos pontos IIL/IIH; ausência usa 0/VCC |
| `output_high_resistance`, `output_low_resistance` | Resistências distintas; ausência usa rout |
| `quiescent_low` | ICC com todas as saídas em LOW; iq permanece o valor HIGH |
| `switching_capacitance` | CPD por entrada que comuta |
| `source_rating`, `sink_rating` | Correntes de referência para avisos por saída |
| `current_rating_window` | Constante de tempo do filtro de corrente para avisos; padrão 1 µs |

`rout` continua como parâmetro comum dos arquivos comportamentais anteriores;
as resistências específicas prevalecem quando declaradas. Os recursos são
optativos: os outros modelos digitais não recebem automaticamente esses
parâmetros novos. Esta etapa requer o compilador atualizado junto dos arquivos;
a instalação local é atualizada após compilar e validar. O editor visual
preserva os limiares absolutos e os múltiplos avisos ao expor o núcleo do modelo.
No SPICE já compilado, esses metadados usam `logic.threshold_reference_parameter`
e a lista `current_warnings` de `{label, signal, limit_parameter}`.

As bancadas estão em `qa/tests/spice/test_schmitt_electrical_models.cpp`.
Elas executam o ngspice nativo, verificando todos os inversores, histerese,
partida indefinida, perda de alimentação, limiares TTL em três alimentações,
correntes de entrada, fan-out 20/21, quedas sob carga, corrente de repouso,
atraso carregado, consumo por frequência com/sem carga externa, ausência de
aviso espúrio em bordas capacitivas e largura de banda sob carga.
A auditoria Python confere as associações e pinagens contra a biblioteca local.

Validação desta etapa: **298 casos nativos aprovados** na suíte completa
(174,30 s), incluindo 12 bancadas elétricas novas e a preservação dos metadados
no editor visual. Os seis testes Python e a auditoria da biblioteca também
passaram: **356 modelos, 507 associações compatíveis**, sem ambiguidade ou
incompatibilidade de pinagem. A instalação local recebeu os cinco arquivos
novos/atualizados e o motor recompilado. Reinicie o CA para carregar esse motor.
