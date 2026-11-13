# Portas pequenas e interfaces AHC/AHCT

Este lote acrescenta **114 modelos executáveis** e **126 vínculos explícitos**
com a biblioteca padrão. O catálogo passa de **419 para 533 modelos** e de
**642 para 767 entradas eletrônicas associadas**. Um dos vínculos explícitos,
`4xxx_IEEE:4013`, já era resolvido pelo nome; por isso o ganho de cobertura é
de 125 entradas. Os conectores e símbolos sem função elétrica permanecem fora
do catálogo de modelos.

## Famílias e funções

Os sufixos das colunas 1G/2G/3G completam o nome da família: por exemplo,
`LVC`, `2G`, `125` representam `74xGxx:74LVC2G125`.

| Família | 1G | 2G | 3G | Outros em `74xx` | Modelos |
|---|---|---|---|---|---:|
| AHC | 00, 02, 04, 08, 125, 126, 14, 32, 86 | 00 | — | 74AHC04, 240, 244 | 13 |
| AHCT | 00, 02, 04, 08, 125, 126, 14, 32, 86 | 00 | — | 74AHCT04, 240, 244 | 13 |
| AUC | 00, 02, 04, 06, 07, 08, 125, 126, 14, 17, 19, 240, 32, 86 | 00, 02, 04, 06, 07, 08, 125, 126, 240, 241, 32, 34, 86 | — | — | 27 |
| AUP | 00, 02, 04, 06, 07, 08, 125, 126, 14, 17, 240, 32, 34 | — | — | — | 13 |
| LVC | 00, 02, 04, 06, 07, 08, 10, 11, 125, 126, 14, 17, 18, 19, 139, 240, 27, 32, 34, 38, 86, 0832, 3208, 332, 386 | 00, 02, 04, 06, 07, 08, 125, 126, 14, 17, 157, 240, 241, 32, 34, 38, 86 | 04, 06, 07, 14, 17, 34 | — | 48 |

As funções 00/10/38 são NAND; 02/27 são NOR; 04/06/14 são inversoras;
07/17/34 são buffers; 08/11 são AND; 32/332 são OR; 86/386 são XOR.
10/11/27/332/386 têm três entradas. 06/07/38 têm dreno aberto, com o HIGH
elétrico obtido por pull-up externo. O modelo não injeta corrente de HIGH
nessas saídas.

Os 14/17 usam histerese com retenção entre os dois limiares. Os 244 deste lote
também conservam o estado de entrada dentro da banda configurada. O estado
inicial dentro da banda é indefinido; o modelo não inventa o último nível.

O catálogo recebe vínculos explícitos para DBV/DCK/DRL de
`SN74LVC1G00`, `SN74LVC1G125`, DBV/DRL de `SN74LVC1G14` e DBV de
`SN74LVC2G14`. As duas variantes `74LS240_Split` e `74LS244_Split` usam os
modelos existentes, conservando seus bancos físicos. Nenhuma associação foi
inferida apenas pelo encapsulamento ou pelo número de pinos.

## Saídas e seleção

| Função | Resultado |
|---|---|
| 1G125/2G125 | Buffer; cada OE em LOW habilita seu canal |
| 1G126/2G126 | Buffer; cada OE em HIGH habilita seu canal |
| 1G240/2G240 | Inversor; cada OE em LOW habilita seu canal |
| 2G241 | Dois buffers; primeiro OE ativo em LOW, segundo ativo em HIGH |
| LVC1G18 | Dado no pino 3; seletor 1 escolhe Y0=6 ou Y1=4; a saída não selecionada fica em Z |
| AUC1G19/LVC1G19 | Endereço 1, desabilitação 3; desabilitar força ambas as saídas HIGH, sem Z |
| LVC1G139 | Endereço A=1/B=2; uma saída ativa em LOW; não possui enable |
| LVC2G157 | Um multiplexador 2:1 com saídas complementares; G=7 em HIGH força Y=5 LOW e /Y=3 HIGH |
| LVC1G0832 | Y = (A AND B) OR C |
| LVC1G3208 | Y = (A OR B) AND C |

Nos drivers duplos, os OEs dos pinos 1/7 controlam os canais de dados
2→6 e 5→3. Não são um enable global. No 2G241, inverter a polaridade do segundo
OE seria um erro funcional apesar de a pinagem coincidir com a do 2G125.

Nas funções de três entradas em seis pinos, A/B/C estão em 1/3/6, Y em 4,
VCC em 5 e GND em 2. No LVC1G139, A é o bit menos significativo; as saídas
Y0/Y1/Y2/Y3 estão em 7/6/5/3. No LVC2G157, A/B estão em 1/2 e o seletor
6 em HIGH escolhe B. Esse componente tem um único multiplexador e não dois
canais independentes.

Os octais 240/244 preservam os bancos
2/4/6/8→18/16/14/12 e 17/15/13/11→3/5/7/9, com enables 1/19 ativos em LOW.
O 240 inverte os dados; o 244 os conserva.

## Parâmetros e limites

| Família | Alimentação aceita | Conversão de entrada padrão | Rout nominal |
|---|---|---|---:|
| AHC | 2–5,5 V | Proporcional a VCC−GND | 35 Ω |
| AHCT | 4,5–5,5 V | LOW/HIGH absolutos de 0,8/2 V | 35 Ω |
| AUC | 0,8–2,7 V | Proporcional a VCC−GND; nominal em 1,8 V | 35 Ω |
| AUP | 0,8–3,6 V | Proporcional a VCC−GND; nominal em 3,3 V | 100 Ω |
| LVC | 1,65–5,5 V | Proporcional a VCC−GND; nominal em 3,3 V | 25 Ω |

Os arquivos incluem atrasos por bloco, capacitância de entrada e consumo fixo
do encapsulamento. Os atrasos nominais são 2–7 ns por bloco; caminhos compostos
acumulam atraso. Esses valores são aproximações próprias a temperatura ambiente,
não tabelas de temporização por carga, alimentação, temperatura ou encapsulamento.
Rout é finito e permite a interação elétrica com cargas; não limita corrente.

Nos LVC, **os limiares DC segmentados e a compatibilidade TTL não são
reproduzidos**: os padrões são 0,3/0,7 da alimentação. A utilização padrão é
com níveis próximos aos trilhos. Isso pode classificar como X uma tensão
intermediária que um componente real aceitaria como HIGH; os parâmetros podem
ser ajustados à bancada. Os 14/17 usam 0,3/0,6 para representar histerese, e os
AHCT1G14 usam 0,8/1,6 V. Os AHCT 04/240/244 têm limiares absolutos de 0,8/2 V.
A pequena histerese intrínseca de outras funções não é representada.

Fora da alimentação válida, os drivers ficam em Z. Enable indefinido não
comprova habilitação e libera a saída, seguindo a convenção tri-state existente.
Em dreno aberto, isso também libera o caminho de sink quando seu controle é X.
Não há proteção térmica, ESD, clamp de entrada, alimentação parasitária,
limitação de corrente ou calibração de consumo de comutação.

Os NC expostos pela biblioteca não são entradas nem drivers. Uma fonte de
corrente de 0 A mantém seu nó no solver para permitir monitoramento; a única
carga em um NC solto é o `rshunt` numérico global já usado pela sessão. Quando
o símbolo omite o NC físico, o arquivo conserva essa mesma lista de terminais.

## Fontes e seleção de componentes

Cada `.casim` registra as referências oficiais em `sources`. Exemplos:

- [Nexperia AHC/AHCT1G00](https://assets.nexperia.com/documents/data-sheet/74AHC_AHCT1G00.pdf),
  [AHC/AHCT1G14](https://assets.nexperia.com/documents/data-sheet/74AHC_AHCT1G14.pdf) e
  [AHC/AHCT244](https://assets.nexperia.com/documents/data-sheet/74AHC_AHCT244.pdf).
- [TI AUC1G00](https://www.ti.com/lit/ds/symlink/sn74auc1g00.pdf),
  [AUP1G00](https://www.ti.com/lit/ds/symlink/sn74aup1g00.pdf) e
  [LVC1G00](https://www.ti.com/lit/ds/symlink/sn74lvc1g00.pdf).
- [TI LVC2G241](https://www.ti.com/lit/ds/symlink/sn74lvc2g241.pdf),
  [LVC1G18](https://www.ti.com/lit/ds/symlink/sn74lvc1g18.pdf),
  [LVC1G139](https://www.ti.com/lit/ds/symlink/sn74lvc1g139.pdf) e
  [LVC2G157](https://www.ti.com/lit/ds/symlink/sn74lvc2g157.pdf).

O AUC1G18 foi deixado sem vínculo: foi encontrado no
[guia antigo da TI](https://www.ti.com/jp/lit/sg/jajt001d/jajt001d.pdf) como produto
planejado, sem datasheet específico verificável. Os desenhos IEEE de 4002,
4012 e 4072 também não receberam vínculo: omitem um terminal que os modelos
existentes usam. A quantidade de entradas da biblioteca não substitui essa
verificação de existência e pinagem.

## Validação e uso

As bancadas de `qa/tests/spice/test_small_logic_models.cpp` executam o solver
com os pinos físicos e resultados booleanos independentes dos blocos dos modelos:

- Todas as combinações de dados e enables das 99 portas pequenas iniciais.
- Todos os bits das seis novas interfaces AHC/AHCT e combinações independentes
  dos dois bancos dos octais.
- Tabelas completas das nove funções compostas, decodificadores e multiplexadores.
- Perda de alimentação em todos os modelos; extremos da faixa nas famílias
  básicas; limiares TTL absolutos dos AHCT.
- Retenção dos Schmitt, NC sem conversores/cargas de entrada e aliases de pacote.

A suíte completa passou com **367 casos e 390.556 verificações**, incluindo
as seis bancadas deste lote. Os seis testes Python das ferramentas também
passaram. Na instalação local, os **533 arquivos** foram conferidos por hash
contra os fontes; os dois testes adicionais de resolução e aliases passaram
com 1.384 verificações. A auditoria do catálogo instalado confirma as mesmas
767 associações, sem ambiguidades ou incompatibilidades de pinagem.

As 123 associações dos arquivos novos e os três vínculos explícitos de arquivos
existentes têm a pinagem conferida. A auditoria global deve permanecer sem
ambiguidades ou pinagens incompatíveis:

```bash
python3 tools/circuitalive/model-catalog.py --fail-on-errors
python3 tools/circuitalive/update-model-translations.py --check
tools/circuitalive/build.sh --target qa_interactive_sim
build/circuitalive/qa/tests/spice/qa_interactive_sim --run_test=SmallLogic*
```

Os arquivos são carregados em tempo de execução. No launcher local, Stop → Play
recria a sessão com os modelos novos. Uma associação explícita ou um perfil
pessoal anterior continua tendo prioridade; nesse caso, escolha o novo modelo
em **Simulation model...**. Os arquivos também são distribuídos na pasta de
modelos da instalação.
