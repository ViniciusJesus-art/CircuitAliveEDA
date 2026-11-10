# Roteamento analógico e proteção a 25 °C

Este lote adiciona 44 arquivos em `simulation/models/`: nove para chaves e
multiplexadores analógicos e 35 para completar a família BZV55. São 95 novas
associações automáticas; as 97 associações dos arquivos deste grupo foram
conferidas contra os pinos da biblioteca padrão instalada.

## Chaves e multiplexadores

| Modelo | Função | Alimentação lógica | RON de referência / span analógico |
|---|---|---|---|
| CD4016B | Quatro chaves SPST independentes | 3–18 V | 580 / 250 / 200 Ω a 5 / 10 / 15 V, aproximação junto aos trilhos |
| CD4066B | Quatro chaves SPST independentes | 3–18 V | 470 / 180 / 125 Ω a 5 / 10 / 15 V |
| CD4051B | 8:1 | 3–18 V | 470 / 180 / 125 Ω a 5 / 10 / 15 V |
| CD4052B | Dois bancos 4:1 com endereço comum | 3–18 V | 470 / 180 / 125 Ω a 5 / 10 / 15 V |
| CD4053B | Três bancos 2:1 com seletores independentes | 3–18 V | 470 / 180 / 125 Ω a 5 / 10 / 15 V |
| 74HC4051 | 8:1 | 2–10 V | 100 / 90 / 70 Ω a 4,5 / 6 / 9 V |
| 74HCT4051 | 8:1, entradas TTL | 4,5–5,5 V | 100 / 90 / 70 Ω a 4,5 / 6 / 9 V |
| CD74HC4067 | 16:1 | 2–6 V | 70 / 60 Ω a 4,5 / 6 V |
| HEF4067B | 16:1 | 3–15 V | 350 / 80 / 60 Ω a 5 / 10 / 15 V |

Os modelos representam caminhos bilaterais de condutância positiva. Passam
tensões intermediárias e, com alimentação apropriada, negativas nos dois
sentidos. Não convertem a tensão do sinal em nível lógico. Há resistência
finita, fuga de desligamento de 1 TΩ por caminho, capacitância de porta,
capacitância de controle, consumo quiescente aproximado e aviso de corrente
de 10 mA por caminho. Esses parâmetros são editáveis.

CD4016/4066 têm enables ativos em HIGH e referenciados a VSS. Os demais
possuem inhibit ativo em HIGH: este abre todos os caminhos. Nos 4051/52/53,
VEE é o trilho analógico negativo, enquanto os controles se referem a VSS/GND.
VEE deve ser menor ou igual a VSS. O span VCC−VEE máximo é 18 V nos CD405x
e 10 V nos HC/HCT4051. Nos 4067, os sinais ficam entre VSS/GND e VCC.

Controles na região entre VIL e VIH não selecionam canais. O teste de presença
de driver impede que uma entrada aberta seja interpretada como endereço zero,
inclusive na partida. Cada controle tem fuga de 1 TΩ para a referência lógica.
HCT usa limiares proporcionais
calibrados em 0,8/2 V a 5 V. As entradas devem ser conectadas a níveis válidos.

A resistência interpola os pontos da tabela, permanecendo no primeiro/último
valor fora deles. A dependência com a tensão do sinal não é reproduzida;
isso é especialmente relevante para o CD4016 no meio dos trilhos. A precisão
da resistência perto da tensão mínima de operação é limitada.

O controle é processado por eventos XSPICE. CD405x adota 450 ns para ligar e
200 ns para desligar; HC/HCT4051, 40/20 ns; HC4067, 50/20 ns; HEF4067,
190/80 ns. A diferença aproxima um intervalo aberto durante a troca de canal.
CD4016/4066 usa 35 ns nos dois sentidos. São valores fixos de referência,
sem ajuste automático do atraso por alimentação/carga, com transição de 2 ns.
Os caminhos abrem imediatamente se a alimentação sair da janela válida.
A resolução temporal do circuito deve acompanhar os sinais de controle.
As bancadas de comutação usam passo de 50 ns e a de atraso usa 1 ns;
alterações abruptas de fontes interativas são observadas nos pontos do solver.

As capacitâncias dos controles e portas analógicas são estimativas agrupadas
de 5 pF. Não há diodos de proteção, alimentação parasitária, injeção de carga,
modelo completo de crosstalk, distorção, aquecimento ou destruição. Respeite os
trilhos nos sinais analógicos; excedê-los não reproduz o comportamento físico.
Os modelos solicitam Gear de ordem 2 para amortecer transientes de comutação;
a opção afeta o circuito inteiro. Não impõem passo máximo global de nanossegundos.

Fontes dos parâmetros e das pinagens: datasheets oficiais
[CD4016B](https://www.ti.com/lit/ds/symlink/cd4016b.pdf),
[CD4066B](https://www.ti.com/lit/ds/symlink/cd4066b.pdf),
[CD4051B/52B/53B](https://www.ti.com/lit/ds/symlink/cd4051b.pdf),
[74HC/HCT4051](https://assets.nexperia.com/documents/data-sheet/74HC_HCT4051.pdf),
[CD74HC4067](https://www.ti.com/lit/ds/symlink/cd74hc4067.pdf) e
[HEF4067B](https://assets.nexperia.com/documents/data-sheet/HEF4067B.pdf).
Os arquivos registram suas referências individuais em `sources`.

## Zeners BZV55

A família está completa para as 74 entradas B/C da biblioteca local: 37
tensões de 2,4 a 75 V. Os graus de tolerância compartilham o modelo nominal;
não há sorteio de tolerância de fabricação. Cada tensão tem associação
explícita e pinagem de cátodo 1 / ânodo 2.

Os 35 novos modelos usam ramos direto e reverso separados. A tensão nominal
é calibrada em 5 mA até 24 V e em 2 mA de 27 a 75 V, seguindo a corrente de
ensaio de cada grupo do
[datasheet BZV55 da Nexperia](https://assets.nexperia.com/documents/data-sheet/BZV55_SER.pdf).
A resistência do ramo reverso desconta a resistência incremental do diodo
para aproximar a resistência diferencial típica nesse ponto. O ramo direto
mantém queda independente da resistência Zener.

A fuga usa um resistor fixo escolhido para 1/10 do máximo de datasheet na
tensão de referência. A capacitância constante usa o limite superior de
datasheet a zero volt; não varia com polarização. Os dois modelos anteriores
(5,1 e 12 V) preservam seus parâmetros, com capacitância de 200 pF.
O aviso de corrente adota 400 mW/VZ e não limita artificialmente a corrente.
Os 35 novos modelos também solicitam Gear de ordem 2 para amortecer transientes.

São aproximações compactas. O joelho de baixa corrente, tolerância,
temperatura, energia de surto, degradação e autoaquecimento não são modelos
completos. Não são macromodelos certificados pelo fabricante.

## Validação

As bancadas em `qa/tests/spice/test_routing_protection_models.cpp` verificam:

- Todos os caminhos físicos, combinações de endereço e bancos independentes.
- Condução nos dois sentidos, incluindo tensões negativas com alimentação dupla.
- Inhibit, controles intermediários/abertos, níveis TTL do HCT e perda de alimentação.
- Resistência nos pontos da tabela e intervalo aberto durante uma troca de canal.
- Tensão nominal, resistência diferencial e condução direta nas 74 variantes B/C.

As referências de bancada usam números de pinos e pontos elétricos explícitos,
independentes da ordem das portas nos arquivos `.casim`.

Validação deste lote: **250 testes interativos e 6 testes da ferramenta de
catálogo aprovados**. As seis bancadas novas passaram em **1.545 verificações**;
a auditoria de pinagem validou as **97 associações** do grupo. O catálogo
completo ficou com 342 entradas associadas, sem ambiguidades ou incompatibilidades
de pinos, entre as 21.029 entradas eletrônicas locais.

```bash
python3 tools/circuitalive/model-catalog.py --fail-on-errors
python3 -m unittest discover -s tools/circuitalive/tests -v
tools/circuitalive/build.sh --target qa_interactive_sim
tools/circuitalive/test-interactive.sh
```

O relatório completo mantém as pendências por símbolo em
`.circuitalive/reports/model-catalog.csv`. A associação com pinagem compatível
não certifica a exatidão elétrica nem significa cobertura total da biblioteca.
