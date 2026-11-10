# JFETs, conjuntos de diodos e drivers Darlington

Este lote acrescenta **41 modelos e 49 associações** com a biblioteca padrão:

| Grupo | Componentes | Arquivos | Associações |
|---|---|---:|---:|
| JFETs | 2N3819, BF245A/B/C, MMBFJ111/112/113 e os 12 desenhos genéricos N/P | 19 | 19 |
| Conjuntos Schottky | BAT54A/C/S, AW/CW/SW, ADW e SDW | 8 | 8 |
| Conjuntos de comutação | BAV70/M/T/W/S e BAV99/S | 7 | 7 |
| Drivers Darlington | ULN2003/2004, ULQ2003A/2004A, ULN2803A/2804A, MC1413/NCV1413B e SN75468/69 | 7 | 15 |

São circuitos compactos próprios, sem copiar código SPICE de fabricantes.
As aproximações são nominais à temperatura ambiente; não representam todas
as curvas, dispersão de fabricação ou falhas das peças.

## JFETs

Os modelos usam o dispositivo J nativo e os modelos NJF/PJF de nível 1 do
[ngspice](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf). O canal conduz
com VGS zero e diminui a corrente com polarização reversa do gate. Os modelos
P invertem as polaridades externas. Ambos os tipos usam Vto negativo nas
equações normalizadas do ngspice; o corte externo do tipo P ocorre com VGS
positivo. O gate tem junções PN e capacitâncias não lineares até D e S.

Os parâmetros `idss` e `pinch_voltage` definem
`Beta = idss / pinch_voltage²`. A corrente externa também depende das
resistências série, modulação do canal e temperatura. Os pontos escolhidos
são editáveis e não constituem valores típicos garantidos:

| Modelo | Idss intrínseca | Módulo do corte |
|---|---:|---:|
| Genéricos N/P | 5 mA | 2 V |
| 2N3819 | 10 mA | 4 V |
| BF245A | 4 mA | 1,8 V |
| BF245B | 10 mA | 3,5 V |
| BF245C | 18 mA | 5,5 V |
| MMBFJ111 | 100 mA | 4,5 V |
| MMBFJ112 | 40 mA | 3 V |
| MMBFJ113 | 12 mA | 1,8 V |

As referências são [2N3819 da Central Semiconductor](https://my.centralsemi.com/datasheets/2N3819.PDF),
[faixas BF245 da NXP](https://www.nxp.com/docs/en/data-sheet/BF245A-B-C.pdf),
[BF245 da onsemi](https://www.onsemi.com/download/data-sheet/pdf/bf245a-d.pdf)
e [MMBFJ111/112/113 da onsemi](https://www.onsemi.com/download/data-sheet/pdf/mmbfj113-d.pdf).
Para MMBFJ, Idss tem apenas um mínimo especificado; os pontos escolhidos também
preservam resistências de canal inferiores aos máximos de 30/50/100 ohm.
Ensaiar Idss em 15 V pode ultrapassar a potência contínua do encapsulamento:
as fichas usam condições de ensaio pulsado, e não há autoaquecimento no modelo.

A pinagem segue o símbolo instalado: 2N3819 tem D/G/S nos pinos 1/2/3;
BF245 tem G/S/D; MMBFJ tem D/S/G. BF245 usa a ordem da biblioteca e do estilo
23 da onsemi; o encapsulamento NXP da referência de faixas usa outra ordem.
Não se deve transferir a pinagem entre fabricantes sem conferir a peça real.

Os 12 símbolos Device:Q_NJFET_* e Q_PJFET_* preservam cada permutação física.
Nos desenhos N, um Value como `MMBFJ113` seleciona a peça pelo nome e remapeia
os terminais D/G/S. Os parâmetros dos genéricos são configuráveis; Value
numérico não define seus parâmetros.

Não há diodo de corpo de MOSFET, avalanche de gate/canal, SOA térmica,
autoaquecimento, dano ou calibração de ruído e resposta RF.

## Conjuntos de diodos

Cada junção é um diodo D nativo com corrente de saturação, emissão,
resistência série, capacitância e parâmetro de trânsito próprios do modelo.
Os arranjos preservam anodo comum, catodo comum ou ligações em série.
Os dois pares dos encapsulamentos de seis pinos têm referências galvânicas
separadas, inclusive nos símbolos desenhados em unidades diferentes.

As topologias e pontos de referência vêm das fichas
[BAT54A/C/S da Vishay](https://www.vishay.com/docs/86410/bat54_bat54a_bat54c_bat54s.pdf),
[BAT54AW/CW/SW da Nexperia](https://assets.nexperia.com/documents/data-sheet/BAT54W_SER.pdf),
[BAT54ADW/SDW da Diodes](https://www.diodes.com/assets/Datasheets/BAT54TW_ADW_CDW_SDW_BRW.pdf),
[BAV70 da Nexperia](https://assets.nexperia.com/documents/data-sheet/BAV70_SER.pdf)
e [BAV99 da Nexperia](https://assets.nexperia.com/documents/data-sheet/BAV99_SER.pdf).

Há inconsistências de texto na biblioteca instalada que não alteram as
ligações físicas usadas pelos modelos:

- BAV99: os pinos são 1 anodo, 2 catodo e 3 ponto intermediário; os rótulos
  K/A/K do símbolo não descrevem essa ligação.
- BAV99S: os pares são **1 → 6 → 2** e **4 → 3 → 5**; os rótulos dos pontos
  intermediários estão trocados no símbolo.
- BAT54SDW: também possui os pares **1 → 6 → 2** e **4 → 3 → 5**;
  a descrição do símbolo menciona anodo comum, embora sejam pares em série.

Schottky usa 30 V reversos e começa com trânsito zero; BAV usa 100 V e
trânsito de 2 ns. Esses valores não certificam curvas de avalanche ou tempos
de recuperação. Os avisos comparam o maior módulo de corrente de um ramo com
o limite configurado. BAV99 usa 125 mA de forma conservadora para carregamento
dos dois diodos. O limite não soma as correntes nem avalia potência total,
temperatura, derating ou energia de surto.

Corrente e potência exibidas usam o primeiro par de portas anodo/catodo;
com excitação em outros terminais, não representam a perda total do conjunto.

## Drivers Darlington

Cada canal possui dois NPN nativos, resistores de entrada e entre base/emissor,
diodo de retorno ao COM e aproximações dos caminhos parasitas negativos.
Os modelos mantêm corrente de entrada e transferência analógica; não usam
uma chave digital ideal para substituir os transistores.

| Família | Canais | Resistor de entrada | Primeira descarga B-E | Classe de saída |
|---|---:|---:|---:|---:|
| ULN/ULQ2003, ULN2803 | 7 ou 8 | 2,7 kohm | 7,2 kohm | 50 V |
| ULN/ULQ2004, ULN2804 | 7 ou 8 | 10,5 kohm | 7,2 kohm | 50 V |
| MC1413, MC1413B, NCV1413B | 7 | 2,7 kohm | 5 kohm | 50 V |
| SN75468 | 7 | 2,7 kohm | 7,2 kohm | 100 V |
| SN75469 | 7 | 10,5 kohm | 7,2 kohm | 100 V |

A descarga B-E do transistor de saída é de 3 kohm. Os valores e circuitos
de referência estão em [ULN200x da TI](https://www.ti.com/lit/ds/symlink/uln2003a.pdf),
[ULN280x da ST](https://www.st.com/resource/en/datasheet/uln2801a.pdf),
[MC1413 da onsemi](https://www.onsemi.com/download/data-sheet/pdf/mc1413-d.pdf)
e [SN7546x da TI](https://www.ti.com/lit/ds/symlink/sn75468.pdf).

As saídas são coletores abertos: precisam de carga ou pull-up externo.
**COM é o retorno dos diodos de roda livre**, sem função de alimentação ou
enable. Ele pode ficar aberto em cargas resistivas; com cargas indutivas,
conecte-o ao retorno apropriado para a energia da carga. Entradas abertas
são descarregadas pelos resistores internos.

Ganhos, capacitâncias e armazenamento são aproximações. O diodo de clamp
tem ruptura reversa aproximada de 50 ou 100 V; avalanche coletor/emissor não
foi implementada. Não use desligamento indutivo sem clamp como resultado
de ruptura confiável. Os caminhos parasitas negativos não são caminhos
especificados para corrente de carga.

O aviso de 0,5 A usa o maior módulo de corrente de uma saída, incluindo o
retorno pelo clamp. Não há limitação artificial, proteção térmica nem cálculo
da capacidade conjunta dos canais. A medição exibida usa o coletor do canal 1
e GND; não soma as perdas do CI. Modelos ULN2002/2801/2802/2805 não foram
incluídos neste lote.

Todos os novos semicondutores solicitam integração Gear de ordem 2 para
mudanças abruptas de fonte, sem impor um passo global pequeno.

## Validação

As bancadas de `qa/tests/spice/test_jfet_diode_driver_models.cpp` verificam:

- Condução sem polarização de gate, corte, região ôhmica, troca D/S e junção
  de gate dos JFETs N/P, além do remapeamento de seis desenhos genéricos N.
- Condução e bloqueio de todas as 38 junções, incluindo os terminais comuns,
  e independência dos pares flutuantes de seis pinos.
- Todos os canais das 15 associações Darlington, correntes de entrada,
  entradas abertas, COM flutuante e avisos por maior corrente de canal.
- Corrente indutiva interrompida retornando ao COM, nas sete implementações
  de driver, com polaridade e tensão de clamp conferidas.
- Avisos de corrente com a configuração completa de sinais, incluindo
  conjuntos de diodos, pontes, relés e chaves analógicas de lotes anteriores.

O gerador de circuitos também foi corrigido para transformar expressões de
aviso, como módulo e máximo de correntes, em sinais nativos executáveis.
Os avisos permanecem habilitados durante esses testes.

`validate-model-pins.py` confere as 49 associações com a biblioteca instalada;
`model-catalog.py --fail-on-errors` atualiza a cobertura e procura ambiguidades
ou pinagens incompatíveis.

Na validação deste lote, passaram os **286 testes nativos**, incluindo as
11 novas bancadas e suas 3.183 verificações, e os seis testes Python da
auditoria. As 49 associações novas passaram na conferência de pinagem.
