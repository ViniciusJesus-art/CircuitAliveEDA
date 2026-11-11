# Catálogo de modelos e expansão da biblioteca

Os modelos executáveis ficam em `simulation/models/*.casim`. Cada arquivo
contém comportamento, parâmetros, pinagem e suas associações em `matches`.
O catálogo e o relatório abaixo são derivados desses arquivos: não há uma
lista paralela de componentes autorizados na simulação.

O catálogo atual tem **419 arquivos `.casim`** e **642 associações automáticas
com pinagem compatível** nas 21.029 entradas eletrônicas da biblioteca local.
Restam **20.387 entradas sem associação explícita** (cobertura de **3,053%**).
Conectores e outros itens não eletrônicos são excluídos. As etapas abaixo registram
os lotes; os números incluem variantes de desenho e encapsulamento.

As associações por Value/Sim.Name e cópias de bibliotecas locais são adicionais
a essa contagem. Veja [Associações automáticas](MODEL_ASSOCIATIONS.md).

## Primeiro lote de expansão da biblioteca

Esta etapa acrescentou **58 modelos**, passando de **59 para 117 arquivos
`.casim`**, incluindo os modelos básicos e fontes que já existiam.

| Família | Componentes novos | Arquivos |
|---|---|---:|
| TTL 74LS | 74LS00, 02, 04, 10, 11, 20, 21, 27, 30, 74, 86, 125, 126, 240 e 244 | 15 |
| CMOS 74HC | 74HC240 e 74HC244 | 2 |
| CMOS com entrada TTL 74HCT | 74HCT00, 02, 04, 74, 240 e 244 | 6 |
| CMOS 4000 | 4001, 4002, 4011, 4012, 4013, 4023, 4025, 4069, 4070, 4071, 4072, 4073, 4075, 4077 e 4081 | 15 |
| Retificadores de 3 A | 1N5400 até 1N5408 | 9 |
| Schottky | 1N5817, 1N5818, 1N5820, 1N5821 e 1N5822 | 5 |
| MOSFETs | 2N7002, BSS138, BSS84, IRF3205, IRLZ44N e IRF9540N | 6 |

Os nomes dos símbolos são os da biblioteca instalada: por exemplo,
`4xxx:4013`, `74xx:74HCT00` e `Transistor_FET:BSS84`.
As variantes IEEE com a mesma função e pinagem compartilham o mesmo arquivo
quando existe uma associação explícita em `matches.library_ids`.
As 58 adições têm **71 associações conferidas contra a pinagem da biblioteca**.

Na biblioteca padrão local, com 21.007 entradas eletrônicas depois das
exclusões, o relatório passou de **77 para 148 associações automáticas com
pinagem compatível**. Restam **20.859 entradas sem associação automática**.
Esses números incluem desenhos/encapsulamentos distintos do mesmo componente;
não significam 21.007 comportamentos eletrônicos independentes.
Os números são um retrato desta biblioteca; o relatório pode ser atualizado.

## Segundo lote: analógicos, alimentação e isolamento

Foram adicionados **49 arquivos e 75 associações**, com parâmetros nominais a
**25 °C** e prioridade para alimentação, corrente nos pinos e resposta no tempo.

| Grupo | Exemplos | Arquivos |
|---|---|---:|
| Operacionais | LM358/324/2904/2902, TL071/072/074, LM741, MCP6001/2/4 | 15 |
| Comparadores | LM393/339/2903/2901 e LMV393 | 5 |
| Reguladores | AMS1117, LM1117, L7905/12/15, L78L05/09/12 | 21 |
| Optoacopladores | PC817, EL817, LTV-817 e TLP291 | 4 |
| Potenciômetros | Simples, duplos e com pino de montagem | 4 |

As pinagens de encapsulamentos diferentes têm arquivos próprios quando
necessário. Limites, parâmetros e condições de uso estão no
[catálogo analógico](ANALOG_MODEL_CATALOG.md).

## Terceiro lote: roteamento analógico e família Zener BZV55

Este lote acrescenta **44 arquivos e 95 associações** sobre o estado anterior
de 185 arquivos e 247 associações, que já incluía memórias e microcontroladores.

| Grupo | Componentes | Arquivos novos | Associações novas |
|---|---|---:|---:|
| Chaves bilaterais | CD4016B e CD4066B, incluindo desenhos IEEE e encapsulamentos compatíveis | 2 | 8 |
| Multiplexadores | CD4051B/52B/53B, 74HC4051, 74HCT4051, CD74HC4067 e HEF4067B | 7 | 15 |
| Zeners | BZV55 de 2,4 a 75 V, graus B/C | 35 | 72 |

Os 37 modelos de tensão da BZV55 agora cobrem todas as 74 entradas B/C dessa
família na biblioteca instalada. Os arquivos anteriores de 5,1 e 12 V mantêm
seus parâmetros elétricos e passam a atender também o grau B.
Os limites, referências e bancadas estão em
[Roteamento e proteção](ROUTING_PROTECTION_MODELS.md).

A ferramenta de auditoria também reconhece os arquivos de schema 2 com
`ngspice+runtime`, usando os mesmos pares de versão/backend do carregador.

## Quarto lote: registradores, decodificadores e contadores

Foram acrescentados **35 arquivos e 42 associações**, passando de 229 arquivos
e 342 associações para 264 arquivos e 384 associações.

| Grupo | Componentes | Arquivos |
|---|---|---:|
| Deslocamento serial | 164, 165 e 595 nas famílias presentes na biblioteca | 9 |
| Registradores e latches paralelos | 273, 373 e 374 em HC/HCT/AHC/AHCT/LS; 174/175 em LS | 17 |
| Decodificadores | 138 em HC/HCT/LS e 139 em LS | 4 |
| Contadores síncronos e ripple | 74LS161, 74LS163, 74LS393, CD4020B e CD4040B | 5 |

Os modelos representam os bancos físicos, clocks, resets, enables e estados
internos específicos de cada CI, incluindo a separação entre deslocamento e
armazenamento de saída no 595. Os contadores CD incluem todos os estágios,
inclusive os dois sem pinos externos no 4020.
Veja [Registradores e contadores](REGISTER_COUNTER_MODELS.md) para funcionamento,
parâmetros, fontes oficiais, limitações e bancadas de referência.

## Quinto lote: redes resistivas, pontes e relés

Foram acrescentados **48 arquivos e 70 associações**:

| Grupo | Componentes | Arquivos | Associações |
|---|---|---:|---:|
| Redes resistivas | R_Network03–13, incluindo Split/US; Dividers x02–x11 SIP | 21 | 43 |
| Pontes retificadoras | Cinco pinagens genéricas, ABS2–10, MB2S/4S/6S/8S e W005G–W10G | 21 | 21 |
| Relés DC não retentivos | SPST-NO/NC, SPDT, DPST-NO/NC e DPDT | 6 | 6 |

As redes preservam barramentos e ramos físicos; as pontes têm quatro diodos
e pinagens específicas por família. Os relés incluem bobina R/L, histerese,
atrasos e contatos isolados com comutação sem sobreposição.
Veja [Redes, pontes e relés](PASSIVE_BRIDGE_RELAY_MODELS.md) para parâmetros,
referências, limites e bancadas. Os relés e pontes genéricas precisam ser
configurados para a peça real; seus padrões não certificam uma peça comercial.

## Sexto lote: JFETs, conjuntos de diodos e drivers

Foram acrescentados **41 arquivos e 49 associações**:

| Grupo | Componentes | Arquivos | Associações |
|---|---|---:|---:|
| JFETs | 2N3819, BF245A/B/C, MMBFJ111/112/113 e 12 desenhos genéricos N/P | 19 | 19 |
| Conjuntos de diodos | BAT54A/C/S e variantes; BAV70/M/T/W/S; BAV99/S | 15 | 15 |
| Drivers Darlington | ULN/ULQ2003/2004, ULN2803/2804, MC/NCV1413 e SN75468/69 | 7 | 15 |

Os JFETs têm canal normalmente condutor e junções de gate; os conjuntos
de diodos preservam cada junção e os pares isolados. Os drivers usam dois NPN
por canal, com corrente de entrada, coletores abertos e diodos de retorno ao
COM. Pinagens, referências, parâmetros e limites estão em
[JFETs, diodos e drivers](JFET_DIODE_DRIVER_MODELS.md).

## Sétima etapa: Schmitt e carga elétrica digital

Foram acrescentados **3 arquivos e 4 associações**: 74HC14, 74LS14 e CD40106B,
incluindo o desenho IEEE do 40106. O total passa de 353/503 para **356 arquivos
e 507 associações**. LS00 e HC00 recebem correntes de entrada, resistências
separadas de saída e avisos de corrente por pino/sentido. LS00 usa limiares
absolutos; HC00/HC14 incluem consumo interno por frequência. As entradas Schmitt
preservam o estado entre seus dois limiares.

Parâmetros, referências e limites estão em
[Portas Schmitt e parâmetros elétricos digitais](SCHMITT_ELECTRICAL_MODELS.md).
Os demais digitais conservam seus modelos anteriores; esses recursos não
significam que todo o catálogo já tenha fan-out e temporização calibrados.

## Oitava etapa: vínculos da biblioteca padrão

A revisão manual acrescenta **110 associações**: 64 em `Device` e 46 em `Switch`.
Além de ampliar os vínculos de seis modelos básicos, há 40 combinações de
primitivas existentes para pacotes resistivos, divisores e chaves com vários
contatos. O total passa a **396 arquivos e 617 associações**. Todas as dez
chaves de dois terminais em `Switch` estão vinculadas, com seus estados iniciais.

Os vínculos são distribuídos nos `.casim`, sem exigir perfis pessoais ou
configuração por instância. Pinagens, limites, exemplos e verificação estão em
[Vínculos revisados com a biblioteca padrão](STANDARD_LIBRARY_ASSOCIATIONS.md).

## Nona etapa: multiplexadores, comparadores, decodificadores e buffers

Foram acrescentados **20 arquivos e 25 associações**, sobre o estado de
399 arquivos e 617 associações encontrado antes deste lote:

| Grupo | Componentes | Arquivos | Associações |
|---|---|---:|---:|
| Multiplexadores | 74LS151/153/157/158/251/253/257/258 | 8 | 10 |
| Comparadores | 85 e 688 em LS/HC/HCT | 6 | 7 |
| Decodificadores ativos em HIGH | 74HC238 e 74HCT238 | 2 | 3 |
| Buffers de oito canais | 541 em LS/HCT/AHC/AHCT | 4 | 5 |

Os modelos distinguem desabilitação com nível fixo de saída e alta impedância,
preservam os controles independentes e incluem a tabela de expansão dos 85.
As cinco variantes IEEE recebem vínculos explícitos. Apenas CIs reais com
função elétrica foram adicionados; conectores e itens sem função de simulação
continuam excluídos. Veja [Lógica combinacional](COMBINATIONAL_LOGIC_MODELS.md)
para pinagens, parâmetros, referências e validação.

## Uso e atualização

Não é necessário recompilar o editor para usar ou editar esses modelos.
O launcher local já aponta `CIRCUITALIVE_MODEL_DIR` para `simulation/models/`.
Use **Stop → Play** para recarregar os arquivos e gerar a nova simulação.
Potenciômetros e trimpots também têm [ajuste de posição durante a execução](LIVE_CONTROLS.md).
O botão **Choose model...** abre busca por nome e navegação por tipo/família.

Uma escolha explícita anterior ou um perfil pessoal conserva sua prioridade.
Se uma instância já tinha outro modelo salvo, selecione o modelo desejado em
**Simulation model...**. Os modelos novos também podem ser usados como blocos
no editor visual de modelos.

Para distribuir os arquivos na instalação local:

```bash
tools/circuitalive/install.sh
```

## Representação e limites

As portas lógicas usam os blocos XSPICE existentes. Incluem atrasos, limiares
de entrada, consumo aproximado, capacitância de entrada e resistência de
saída. Os buffers 125/126 têm enables individuais; os 240/244 têm dois bancos
de enable. O 240 inverte os dados. O 4013 tem SET/RESET ativos em nível alto;
74LS74/74HCT74 têm preset/clear ativos em nível baixo. Entradas abertas ficam
indefinidas; buffers desabilitados e saídas sem alimentação ficam em Z.

Os parâmetros digitais representam pontos nominais próximos de **5 V e
temperatura ambiente**, e podem ser editados. A resistência e o atraso são
fixos: não reproduzem todas as curvas de alimentação, carga e temperatura.
A maioria dos LS/HCT usa limiares proporcionais calibrados para 0,8/2 V a 5 V.
LS00/LS14 já usam limiares absolutos e correntes DC de entrada, com avisos de
fan-out. HC00/HC14 incluem consumo interno por comutação. Essa calibração ainda
não foi aplicada aos demais arquivos. ESD, alimentação parasitária e todas as
violações de setup/hold continuam fora desses modelos.
A família LS mantém queda de nível alto diferente da HCT.
O 4069 é utilizável como inversor digital; seu comportamento linear sem buffer,
necessário em alguns amplificadores/osciladores, ainda não está representado.

Os retificadores usam um elemento D nativo, com resistência, ruptura reversa
e capacitância de junção. O tempo de trânsito aproxima o armazenamento de
carga dos 1N540x; não é um valor de recuperação garantido para qualquer bancada.
Nos Schottky esse parâmetro é zero; há corrente de deslocamento capacitiva.
O limite de corrente gera um aviso instantâneo baseado na corrente média
nominal: ele não impõe limitação artificial, nem avalia aquecimento ou pulsos.

Cada MOSFET usa um VDMOS nativo, incluindo canal N/P, diodo de corpo,
capacitância Miller e resistências internas. Os testes verificam condução,
bloqueio, polaridade, diodo de corpo e faixas de resistência. O IRLZ44N também
é verificado com gate em 4 e 5 V; o limiar de gate não representa a tensão de
acionamento necessária para obter a resistência especificada.
Esses modelos solicitam `.options method=gear maxord=2`, assim como os BJTs
compactos da etapa anterior, para amortecer oscilações numéricas em alterações
abruptas. A opção afeta o integrador do **circuito inteiro**, sem impor passo
máximo global de nanossegundos.

São aproximações próprias, com referências em `sources` e simplificações em
`model_notes`. Não são macromodelos certificados pelos fabricantes. Não
simulam destruição, autoaquecimento, área segura de operação ou todas as curvas
dos datasheets. O custo por componente é um conjunto pequeno de elementos;
a lógica utiliza eventos, sem interpretar um programa por componente no timer
da interface.

As referências incluem os datasheets oficiais de
[74LS00](https://www.ti.com/lit/ds/symlink/sn74ls00.pdf),
[74HCT00](https://www.ti.com/lit/ds/symlink/sn74hct00.pdf),
[CD4001B](https://www.ti.com/lit/ds/symlink/cd4001b.pdf),
[CD4013B](https://www.ti.com/lit/ds/symlink/cd4013b.pdf),
[1N540x](https://www.vishay.com/docs/88516/1n5400.pdf),
[1N581x](https://www.vishay.com/docs/88525/1n5817.pdf),
[1N582x](https://www.vishay.com/docs/88526/1n5820.pdf),
[2N7002](https://assets.nexperia.com/documents/data-sheet/2N7002.pdf),
[BSS138](https://www.onsemi.com/download/data-sheet/pdf/bss138-d.pdf),
[BSS84](https://assets.nexperia.com/documents/data-sheet/BSS84.pdf),
[IRF3205](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irf3205-datasheet-en.pdf),
[IRLZ44N](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irlz44n-datasheet-en.pdf),
[IRF9540N](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irf9540n-datasheet-en.pdf)
e o [manual do ngspice](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).
Cada arquivo registra também a referência específica de seu CI.

## Auditoria e lista de pendências

```bash
python3 tools/circuitalive/model-catalog.py --fail-on-errors
```

São gerados arquivos locais, fora do versionamento:

- `.circuitalive/reports/model-catalog.json`: totais, cobertura por biblioteca
  e associações que não existem na biblioteca instalada.
- `.circuitalive/reports/model-catalog.csv`: uma linha por símbolo eletrônico,
  com modelo associado ou indicação de pendência.

`associated` exige uma associação não ambígua e o mesmo conjunto de pinos
físicos. Isso não equivale a certificar a precisão elétrica. `no_model`
indica pendência; `pin_mismatch` e `ambiguous` são erros de associação.
O relatório não inclui perfis pessoais, associações manuais por instância ou
inferência a partir de campos SPICE legados. Por isso mede a cobertura
explícita do catálogo distribuído, não todos os usos possíveis de um modelo.

Fontes ideais, GND/VCC, conectores, itens gráficos, mecânicos, jumpers e
instrumentos genéricos de medição são excluídos. Relés, chaves, sensores,
transformadores, módulos e outros componentes físicos são incluídos.
Um mesmo modelo pode atender variantes verificadas, mas sem unir famílias
elétricas diferentes ou pinagens incompatíveis.

Para conferir apenas novos arquivos:

```bash
python3 tools/circuitalive/validate-model-pins.py simulation/models/cd4013.casim
```

A leitura da biblioteca está isolada em `tools/circuitalive/symbol_library.py`.
Ela considera pinos sem nome, herança, todas as unidades e o estilo normal do
desenho, sem contar pinos alternativos de De Morgan como novos terminais.

## Validação e próximos grupos

```bash
python3 -m unittest discover -s tools/circuitalive/tests -v
tools/circuitalive/test-interactive.sh --verbose
tools/circuitalive/test-spice.sh --verbose
```

As bancadas novas em `qa/tests/spice/test_model_catalog.cpp` exercitam tabelas
lógicas completas em todos os canais, enables independentes, saída Z,
flip-flops, limiares TTL, atraso em resolução de nanossegundos, quedas diretas,
bloqueio reverso, resistência dos MOSFETs e seus diodos de corpo.

A validação desta etapa passou em **118 testes interativos, 68 testes SPICE
nativos e 3 testes da ferramenta de catálogo**, além da conferência das 71
associações novas com os pinos da biblioteca instalada.

A segunda etapa acrescenta 11 bancadas em
`qa/tests/spice/test_analog_model_catalog.cpp`. A suíte completa passou em
**129 testes interativos, 68 testes SPICE nativos e 3 testes de catálogo**.

A cobertura de toda a biblioteca continua em desenvolvimento. Os próximos
grupos prioritários incluem outras proteções e chaves analógicas, outros contadores
e registradores, sensores, relés e memórias adicionais. Cada grupo deve ter
bancadas de referência, não somente associação de nomes.
Os microcontroladores suportados já executam firmware por meio de backends
próprios, com os periféricos e limitações descritos em
[Modelos de microcontroladores](MICROCONTROLLER_MODELS.md). Outros alvos
exigem implementação e validação específicas da CPU e dos periféricos.
