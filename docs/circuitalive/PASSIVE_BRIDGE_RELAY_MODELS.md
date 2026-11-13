# Redes resistivas, pontes retificadoras e relés

Este lote acrescenta **48 arquivos `.casim` e 70 associações** com símbolos
eletrônicos da biblioteca padrão. Os modelos são circuitos próprios; nenhum
código SPICE de fabricante foi copiado.

| Grupo | Símbolos | Arquivos | Associações |
|---|---|---:|---:|
| Redes com barramento comum | Device:R_Network03 a 13, desenhos normal, Split e US | 11 | 33 |
| Redes divisoras de dois barramentos | Device:R_Network_Dividers_x02_SIP a x11_SIP | 10 | 10 |
| Pontes genéricas | Os cinco desenhos Device:D_Bridge_* | 5 | 5 |
| Pontes ABS | ABS2, ABS4, ABS6, ABS8 e ABS10 | 5 | 5 |
| Pontes MB | MB2S, MB4S, MB6S e MB8S | 4 | 4 |
| Pontes W | W005G, W01G, W02G, W04G, W06G, W08G e W10G | 7 | 7 |
| Relés genéricos de bobina DC | SPST-NO/NC, SPDT, DPST-NO/NC e DPDT | 6 | 6 |

## Redes resistivas

Nas redes R_Network, o pino físico **1 é o barramento comum**. Cada outro
pino se conecta a ele por um resistor separado. **Value** fornece a resistência
de cada ramo: uma rede de oito resistores com Value `10k` tem oito ramos de
10 kohm. Os desenhos Split preservam esse mesmo barramento entre unidades.

As redes Dividers têm dois barramentos, nos pinos **1 e N+2**, e N taps
independentes. Cada tap possui um resistor até cada barramento. Configure
`upper_resistance` e `lower_resistance` em **Simulation model...**; ambos
começam em 10 kohm, sem interpretar Value. Com os taps sem carga:

`Vtap = (VCOM1 / Rupper + VCOM2 / Rlower) / (1/Rupper + 1/Rlower)`.

Uma carga em um tap altera aquele divisor; os demais só são afetados se as
fontes dos barramentos tiverem impedância. A topologia de dois barramentos
segue os circuitos de terminação da [Vishay CSC](https://www.vishay.com/docs/31509/csc.pdf)
e a [nota de terminação dupla](https://www.vishay.com/docs/50064/dualterm.pdf).
Os símbolos genéricos não representam uma configuração comercial específica.

Os resistores são ideais: tolerância, ruído, deriva e falha térmica não são
representados. A medição de potência usa a corrente do primeiro barramento e
a tensão até o primeiro ramo ou o segundo barramento; ela não calcula a soma
das perdas de todos os resistores quando há outras fontes nos taps.

## Pontes retificadoras

Cada ponte contém **quatro diodos nativos**, com duas quedas de junção no
caminho de condução. A fonte AC deve ficar **entre os dois terminais AC**;
o retorno DC pode ter sua própria referência. Os modelos mantêm a pinagem
física específica de cada símbolo:

| Desenho/família | DC + | DC − | AC |
|---|---:|---:|---|
| D_Bridge_+-AA, ABS e MB8S | 1 | 2 | 3, 4 |
| D_Bridge_+A-A e W | 1 | 3 | 2, 4 |
| D_Bridge_+AA- | 1 | 4 | 2, 3 |
| D_Bridge_-A+A | 3 | 1 | 2, 4 |
| D_Bridge_-AA+ | 4 | 1 | 2, 3 |
| MB2S, MB4S e MB6S | 3 | 4 | 1, 2 |

As famílias têm parâmetros compactos nominais a 25 °C. Corrente de saturação,
emissão e resistência série aproximam a condução; capacitância e avalanche
também são configuráveis. Os pontos de referência vêm das fichas
[ABS da Diotec](https://diotec.com/tl_files/diotec/files/pdf/datasheets/abs2.pdf),
[MB2S/4S/6S da Vishay](https://www.vishay.com/docs/88661/mb2s.pdf),
[MB8S da onsemi](https://www.onsemi.com/download/data-sheet/pdf/mb8s-d.pdf)
e [W da Vishay](https://www.vishay.com/docs/88769/w005g.pdf).

As tensões reversas nominais são 200–1000 V para ABS, 200/400/600/800 V para MB
e 50–1000 V para W. O aviso usa 1 A para ABS, 0,5 A para MB e 1,5 A para W.
A corrente ABS segue a ficha atual da Diotec; a descrição de alguns símbolos
ainda informa 0,8 A. MB6S usa a família MBS de 0,5 A compatível com seu desenho;
o link de ficha presente no símbolo instalado aponta para outra família, DFS.

As pontes genéricas começam em **100 V e 1 A**, valores editáveis que não
constituem especificação de uma peça real. A tensão reversa nominal aproxima
o início da avalanche; ela não fornece uma curva de ruptura garantida.
`junction_capacitance` é a capacitância a zero volt, diferente do ponto de
4 V reversos especificado em algumas fichas. `transit_time` começa em zero:
recuperação reversa e perdas de comutação em alta frequência não foram calibradas.

O aviso compara instantaneamente a corrente de saída com o valor médio
nominal; não limita corrente nem avalia pulsos de carga de capacitores.
Não há autoaquecimento, derating, avaliação de energia de surto ou dano.
A integração Gear de ordem 2 evita oscilação numérica de corrente após
mudanças abruptas de fonte, sem impor um passo global pequeno.
A corrente exibida entra em DC + e fica negativa durante o fornecimento de
energia à carga. A potência dessa porta não é a dissipação total da ponte.

## Relés de bobina DC

São modelos genéricos **não retentivos**, com bobina R/L e contatos bilaterais.
Os valores iniciais são **12 V, 400 ohm e 0,8 H**, sem vínculo com uma peça
comercial. Ajuste os parâmetros para o relé utilizado.

O módulo da corrente da bobina aciona uma histerese: pickup em 75% da corrente
nominal `rated_voltage / coil_resistance` e dropout em 10%. Entre os limiares,
o estado anterior permanece. A polaridade da bobina pode ser invertida.
Depois de cada cruzamento há um atraso, inicialmente **5 ms para operar** e
**3 ms para liberar**. A subida/descida da corrente R/L acrescenta seu próprio
tempo: com os padrões e um degrau de 12 V, o pickup ocorre após cerca de
2,77 ms, antes dos 5 ms de atraso.

O percurso do contato dura 200 us: NC abre antes de NO fechar, nas duas
direções. O contato fechado tem 0,05 ohm; aberto, 10 Gohm. Esses parâmetros
são editáveis. Bobina e cada polo têm grupos galvânicos independentes, para
preservar referências e circuitos flutuantes. A implementação usa L e SW
nativos e atrasos XSPICE descritos no
[manual do ngspice](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).
Os contatos leem diretamente a posição contínua da armadura, com uma pequena
histerese de 0,001 na escala normalizada de 0 a 1. Isso substitui as fontes
comparadoras booleanas, que podiam produzir `Timestep too small` durante a
comutação em sessões com pausas frequentes. NC e NO conservam os limiares próximos
de 25% e 75%, o intervalo com ambos abertos e as resistências configuradas.
O controle usa a [histerese nativa do modelo SW](https://nmg.gitlab.io/ngspice-manual/circuitelementsandmodels/elementarydevices/switchmodel_sw_csw.html).

Não existe diodo de supressão interno. O circuito externo define o caminho
da corrente indutiva ao desligar a bobina. Os atrasos são funcionais; não
reproduzem oscilação mecânica, bounce, arco, soldagem de contatos, vida útil,
remanência ou operação de bobina AC. Modelos de relés retentivos não fazem
parte deste lote. O aviso de 60 mA se refere à bobina; não avalia a capacidade
dos contatos. Corrente e potência exibidas também se referem à bobina.

## Bancadas de validação

`qa/tests/spice/test_passive_bridge_relay_models.cpp` exercita:

- Todos os ramos e desenhos das redes com carregamentos diferentes, nos dois
  sentidos de corrente, incluindo a soma das correntes no barramento.
- Todos os taps das redes divisoras, com resistências desiguais, dois níveis
  de barramento e cargas alternadas.
- As 21 pontes nos dois semiciclos e cada um dos quatro caminhos de diodo
  em condução e bloqueio; retificação de frequência dupla e descarga de filtro.
- Os seis relés com ambas as polaridades da bobina, corrente exponencial R/L,
  resistência de contato e referências independentes para cada polo.
- Histerese, atraso total, intervalo com ambos os contatos abertos e mudança
  de tensão nominal/resistência/indutância da bobina.
- Desligamento da bobina por chave externa, com corrente de recirculação
  em um diodo externo e tensão reversa limitada por esse diodo.

A pinagem das 70 associações também é comparada à biblioteca instalada por
`tools/circuitalive/validate-model-pins.py`. O relatório de cobertura é gerado
por `tools/circuitalive/model-catalog.py --fail-on-errors`.

Na validação deste lote, passaram os **275 testes da suíte nativa**, incluindo
as 12 novas bancadas, e os seis testes Python da auditoria. As 70 novas
associações passaram na conferência de pinagem; o catálogo não apresentou
associações ambíguas ou incompatíveis.
