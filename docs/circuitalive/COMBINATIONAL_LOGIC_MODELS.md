# Multiplexadores, comparadores, decodificadores e buffers

Este lote acrescenta **20 modelos `.casim` e 25 vínculos automáticos** com
componentes reais da biblioteca padrão. O comportamento específico fica nos
arquivos de `simulation/models/`, usando as primitivas XSPICE existentes.
Nenhuma lista de peças foi acrescentada ao núcleo C++ do simulador.

## Componentes e vínculos

| Função | Símbolos em `74xx` | Variantes em `74xx_IEEE` |
|---|---|---|
| Multiplexador 8:1 com saídas complementares | 74LS151, 74LS251 | — |
| Dois multiplexadores 4:1 | 74LS153, 74LS253 | — |
| Quatro multiplexadores 2:1 | 74LS157, 74LS158, 74LS257, 74LS258 | 74LS257, 74LS258 |
| Comparador de magnitude de quatro bits | 74LS85, 74HC85, 74HCT85 | — |
| Comparador de igualdade de oito bits | 74LS688, 74HC688, 74HCT688 | 74LS688 |
| Decodificador 3:8 com saídas ativas em HIGH | 74HC238, 74HCT238 | 74HC238 |
| Buffer não inversor de oito canais | 74LS541, 74HCT541, 74AHC541, 74AHCT541 | 74LS541 |

Os vínculos são distribuídos em `matches.library_ids`; perfis pessoais e escolhas
explícitas anteriores conservam sua prioridade. Desenho herdado e quantidade
de pinos não autorizam associar outros CIs. Conectores, itens mecânicos,
gráficos e símbolos sem função elétrica simulável não foram incluídos.

## Seleção e desabilitação

Nos 151/251, D0–D7 são os pinos **4, 3, 2, 1, 15, 14, 13, 12** e S0–S2
são **11, 10, 9**, em ordem de peso crescente. O pino 5 apresenta o dado
selecionado e o 6 seu complemento. O controle 7 é ativo em LOW. Desabilitar
o 151 força **5=LOW, 6=HIGH**; desabilitar o 251 coloca **ambos em Z**.

Nos 153/253, S0/S1 estão em **14/2**. As entradas D0–D3 do primeiro banco
estão em **6, 5, 4, 3**, saída **7**, controle **1**; no segundo banco estão
em **10, 11, 12, 13**, saída **9**, controle **15**. Os controles são
independentes e ativos em LOW. No 153, desabilitar força a saída LOW;
no 253, libera a saída em Z.

Os 157/158/257/258 compartilham os grupos físicos
**(D0,D1,Y) = (2,3,4), (5,6,7), (11,10,9), (14,13,12)**.
O seletor 1 em HIGH escolhe D1. O controle 15 em HIGH força LOW no 157,
HIGH no 158 e Z nos 257/258. Os 158/258 invertem o dado selecionado.
As letras C/D de alguns desenhos da biblioteca variam; os vínculos seguem
os grupos físicos, sem trocar os canais 9/12.

Os 541 têm entradas A0–A7 em **2–9** e saídas Y0–Y7 em
**18, 17, 16, 15, 14, 13, 12, 11**. Ambos os controles **1/19** precisam
estar em LOW; qualquer HIGH coloca todos os canais em Z. Os buffers
acompanham os dados atuais ao habilitar, sem armazenar o valor anterior.

Os 238 usam A0–A2 em **1/2/3** e Y0–Y7 em
**15, 14, 13, 12, 11, 10, 9, 7**. Só habilitam com **4=LOW, 5=LOW, 6=HIGH**.
Uma saída selecionada fica HIGH e as demais LOW. Desabilitar força todas LOW,
sem alta impedância enquanto a alimentação é válida.

## Comparação e cascata

Nos 85, A0–A3 estão em **10, 12, 13, 15** e B0–B3 em **9, 11, 14, 1**.
A3/B3 são os bits de maior peso. As saídas **A>B, A=B, A<B** ficam em **5/6/7**.
As entradas de expansão **IA<B, IA=B, IA>B** estão em **2/3/4**.
Para um comparador isolado, use **2=LOW, 3=HIGH, 4=LOW**.

Quando os quatro bits diferem, o bit mais significativo diferente determina
o resultado, independentemente da expansão. Quando A=B, a tabela inclui todas
as oito combinações de expansão:

| IA<B | IA=B | IA>B | A>B | A=B | A<B |
|---:|---:|---:|---:|---:|---:|
| X | 1 | X | 0 | 1 | 0 |
| 0 | 0 | 0 | 1 | 0 | 1 |
| 0 | 0 | 1 | 1 | 0 | 0 |
| 1 | 0 | 0 | 0 | 0 | 1 |
| 1 | 0 | 1 | 0 | 0 | 0 |

Nos 688, as duas palavras usam os pares físicos
**(2,3), (4,5), (6,7), (8,9), (11,12), (13,14), (15,16), (17,18)**.
A saída **19** fica LOW apenas quando todas as posições coincidem e o
controle **1** está LOW. Desabilitar força HIGH. A saída é totem-pole;
não é coletor aberto nem tri-state.

Na biblioteca local, `74xx_IEEE:74LS688` rotula o pino 1 como `/G1`, mas
marca seu tipo elétrico como saída. O modelo trata esse pino como **entrada
de habilitação**, conforme o datasheet. O vínculo preserva a pinagem física;
o tipo de ERC do símbolo distribuído pela biblioteca não foi alterado.

## Parâmetros elétricos e limites

Todos os modelos incluem resistência finita de saída, capacitância de entrada,
consumo fixo do encapsulamento e atrasos por bloco. A carga altera a tensão e
o tempo de acomodação. As alimentações são **16/8** nos multiplexadores, 85 e
238; **20/10** nos 688 e 541.

| Família | Alimentação aceita | Limiar LOW/HIGH | Saída nominal sem carga | Rout |
|---|---|---|---|---:|
| LS | 4,75–5,25 V | 0,8/2 V absolutos | 0,2 V / VCC−1,6 V | 25 Ω |
| HC | 2–6 V | 0,3/0,7 de VCC−GND | GND / VCC | 45 Ω |
| HCT | 4,5–5,5 V | 0,8/2 V absolutos | GND / VCC | 45 Ω |
| AHC | 2–5,5 V | 0,3/0,7 de VCC−GND | GND / VCC | 35 Ω |
| AHCT | 4,5–5,5 V | 0,8/2 V absolutos | GND / VCC | 35 Ω |

Os níveis absolutos TTL são referidos ao GND local e não escalam com VCC.
Fora da faixa de alimentação, as saídas são liberadas. Entradas sem driver
permanecem X. Um controle de saída indefinido não comprova habilitação;
a saída tri-state permanece liberada. Um controle de desabilitação conhecido
domina os dados flutuantes conforme a função de cada CI.

São aproximações próprias a 25 °C, próximas de 5 V. Os atrasos de **5–18 ns
por bloco** não são uma matriz de tempos por caminho do encapsulamento;
caminhos com várias portas acumulam atraso. As saídas complementares também
não têm uma diferença de atraso caracterizada. Não há calibração de corrente
DC de entrada/fan-out, consumo de comutação, proteção, aquecimento ou dano.
As resistências não impõem limites de corrente.

A pequena histerese das entradas P/Q do LS688 e das entradas do LS541 foi
omitida; essas entradas usam conversão comum 0/X/1. Os vínculos LS257/258 adotam a função e a pinagem
comum documentada para LS257B/258B, sem garantir a capacidade de corrente do
grau B. Esses limites estão também em `model_notes` dos arquivos.

## Referências

Pinagem e função foram verificadas nos documentos oficiais de
[SN74LS151](https://www.ti.com/lit/ds/symlink/sn74ls151.pdf),
[SN74LS153](https://www.ti.com/lit/ds/symlink/sn74ls153.pdf),
[SN74LS157/158](https://www.ti.com/lit/ds/symlink/sn74ls157.pdf),
[SN74LS251](https://www.ti.com/product/SN74LS251),
[SN74LS253](https://www.ti.com/lit/ds/symlink/sn74ls253.pdf),
[SN74LS257B/258B](https://www.ti.com/lit/ds/symlink/sn74ls258b.pdf),
[SN74LS85](https://www.ti.com/lit/ds/symlink/sn74ls85.pdf),
[74HC/HCT85](https://assets.nexperia.com/documents/data-sheet/74HC_HCT85.pdf),
[SN74LS688](https://www.ti.com/lit/ds/symlink/sn74ls688.pdf),
[SN74HC688](https://www.ti.com/lit/ds/symlink/sn74hc688.pdf),
[CD74HCT688](https://www.ti.com/lit/ds/symlink/cd74hct688.pdf),
[74HC/HCT238](https://assets.nexperia.com/documents/data-sheet/74HC_HCT238.pdf),
[SN74LS541](https://www.ti.com/lit/ds/symlink/sn74ls541.pdf),
[74HC/HCT541](https://assets.nexperia.com/documents/data-sheet/74HC_HCT541.pdf),
[SN74AHC541](https://www.ti.com/lit/ds/symlink/sn74ahc541.pdf) e
[SN74AHCT541](https://www.ti.com/lit/ds/symlink/sn74ahct541.pdf).
Cada arquivo registra sua referência em `sources`.

## Validação e uso

As bancadas em `qa/tests/spice/test_combinational_logic_models.cpp` verificam
a pinagem com estímulos e resultados independentes das expressões dos modelos:

- Todos os seletores e pinos de dados dos multiplexadores; nos quádruplos,
  todas as 256 combinações das duas palavras de quatro bits.
- Todos os 256 pares de palavras dos 85 e todas as combinações de expansão,
  incluindo os casos que não usam apenas um sinal de expansão ativo.
- Todas as 256 palavras iguais dos 688, cada uma com oito divergências de um bit.
- Todos os endereços e combinações de habilitação dos 238.
- Todas as 256 palavras dos 541, os dois controles e dados alterados em Z.
- Saídas desabilitadas sob pull-up de 3,3 V, perda de alimentação,
  entradas flutuantes e as cinco associações IEEE.
- Limiares TTL nas extremidades da faixa de alimentação, atrasos de dados
  e habilitação dos buffers, e operação com GND local em 3,3 V.

A auditoria completa passou de **399/617** para **419 modelos/642 associações**,
sem vínculos ambíguos ou pinagens incompatíveis, nas 21.029 entradas
eletrônicas locais. A cobertura permanece parcial.

A suíte completa local passou com **358 casos e 352.280 asserções**, incluindo
os **13 casos novos** deste lote. A execução levou 322,35 s; o limite de tempo
do teste completo no CTest foi ampliado para 600 s para acomodar os catálogos.
As 25 pinagens novas e a extração de textos traduzíveis também foram validadas.

```bash
python3 tools/circuitalive/model-catalog.py --fail-on-errors
python3 tools/circuitalive/validate-model-pins.py simulation/models/74ls151.casim
tools/circuitalive/build.sh --target qa_interactive_sim
tools/circuitalive/test-interactive.sh
```

O launcher local já procura os modelos neste checkout. Use **Stop → Play**
para recarregar os arquivos; não é necessário recompilar o editor para os
novos vínculos. A instalação inclui `simulation/models/*.casim` pelo mecanismo
existente de distribuição de modelos.
