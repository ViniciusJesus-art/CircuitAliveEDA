# Vínculos revisados com a biblioteca padrão

Esta revisão acrescenta **110 associações eletrônicas** aos arquivos distribuídos:
64 em `Device` e 46 em `Switch`. O catálogo passa de 356 arquivos/507 associações
para **396 arquivos/617 associações**, nas 21.029 entradas eletrônicas locais.
Foram atualizados seis modelos básicos e criadas 40 combinações de primitivas
já existentes para representar corretamente símbolos com mais terminais.

Os vínculos estão declarados individualmente em `matches.library_ids` dos
`.casim`; não são perfis pessoais nem campos que o usuário precise salvar em
cada componente. A instalação copia esses arquivos para
`share/circuitalive/models`, que o editor encontra automaticamente. Uma escolha
explícita anterior de modelo ou perfil pessoal conserva sua prioridade.
`symbol_names` mantém o reconhecimento de cópias com os mesmos nomes e pinos.
Não é necessário modificar a biblioteca KiCad de terceiros para distribuir esses
padrões do CA.

## Critério da revisão

Cada associação foi escolhida pela função e pelos terminais do símbolo local.
O número de pinos e a herança do desenho servem para conferir uma escolha,
não para escolher um modelo. Por exemplo, vários reguladores de tensões
diferentes herdam o desenho do L7805; isso não os transforma em reguladores
fixos de 5 V. Contadores decimais e binários também podem compartilhar desenho.
Esses casos continuam sem um vínculo inadequado.

A verificação usa a biblioteca em
`.circuitalive/libraries/usr/share/kicad/symbols`. A contagem exclui conectores,
itens gráficos/mecânicos e símbolos de fonte ideal. O vínculo adicional de
`Simulation_SPICE:D` é instalado, mas não entra nas 110 entradas eletrônicas.

## Chaves de dois terminais

Todas as dez entradas de dois terminais da biblioteca `Switch` agora usam o
modelo `switch`, com resistência de contato e fuga configuráveis:

| Símbolo em `Switch` | Estado inicial |
|---|---|
| SW_SPST | Fechado, preservando o padrão anterior |
| SW_Push | Aberto |
| SW_Push_45deg | Aberto |
| SW_Push_Open | Fechado |
| SW_DIP_x01 | Aberto |
| SW_Omron_B3FS | Aberto |
| SW_Reed | Aberto |
| SW_Reed_Opener | Fechado |
| SW_SPST_Temperature_NO | Aberto |
| SW_SPST_Temperature_NC | Fechado |

O acionamento usa a interação existente de alternar por clique. Os símbolos
reed e térmicos são acionados manualmente; campo magnético, temperatura,
repique e retorno mecânico momentâneo não são simulados nesta etapa.

## Chaves com mais terminais

| Modelo/combinação | Símbolos em `Switch` | Ligação conferida |
|---|---|---|
| switch_shielded | SW_Push_Shielded, CK_KMS2xxG, CK_KMS2xxGP | Contato 1–2; blindagem SH independente |
| switch_dpst | SW_DPST, SW_DPST_x2, SW_DPST_Temperature, SW_Push_Dual, SW_Push_Dual_x2, SW_Push_Open_Dual, SW_Push_Open_Dual_x2 | Dois contatos isolados 1–2 e 3–4; acionamento conjunto |
| switch_tactile_four_pin | SW_MEC_5G | 1/2 permanentemente unidos; 3/4 permanentemente unidos; chave entre os grupos |
| switch_spdt | SW_SPDT, SW_Push_SPDT, SW_Reed_SPDT | Comum 2; repouso 1; outro contato 3 |
| switch_spdt_201 | SW_SPDT_201 | Comum 0; repouso 2; outro contato 1 |
| switch_spdt_312 | SW_SPDT_312, SW_Wuerth_450301014042, SW_SPDT_XKB_DMx-xxxx-1 | Comum 1; repouso 3; outro contato 2 |
| switch_spdt_321 | SW_SPDT_321, SW_Nidec_CAS-120A1 | Comum 2; repouso 3; outro contato 1 |
| switch_dpdt | SW_DPDT_x2, SW_Push_DPDT, SW_Slide_DPDT | Comuns 2/5; repouso 1/4; outros 3/6; dois polos isolados |
| switch_led | SW_Push_LED, SW_SPST_LED, SW_MEC_5E, SW_MEC_5G_LED | Contato 1–2 independente do LED 4(A)/3(K) |
| switch_dip_02 até switch_dip_12 | SW_DIP_x02 até SW_DIP_x12 | N controles independentes; contato i com o pino 2N+1−i |

As chaves iluminadas reutilizam o LED configurável existente, inclusive sua
corrente, brilho e aviso. A cor/queda real do LED não é inferida do nome do
fabricante. O circuito do LED funciona independentemente do estado do contato.
Os novos contatos têm modelos genéricos; não recebem tempos/limites específicos
de cada fabricante apenas por compartilhar o desenho.

A biblioteca `Switch` passou de **2 para 48 entradas vinculadas**, de um total
de 80. Os 32 casos restantes incluem seletores codificados, rotativos, posição
central aberta, módulos com LCD, lâmpadas e outras combinações que exigem
comportamento adicional. Eles não foram reduzidos a uma chave de dois estados.

## Resistores e outros passivos

| Modelo | Símbolos em `Device` |
|---|---|
| resistor | R, R_45deg, R_Small, R_Small_US, R_US, R_Trim, R_Variable, R_Variable_US; Thermistor, Thermistor_US, Thermistor_NTC, Thermistor_NTC_US, Thermistor_PTC, Thermistor_PTC_US |
| capacitor | C, C_45deg, C_Polarized, C_Polarized_Small, C_Polarized_Small_US, C_Polarized_US, C_Small, C_Small_US, C_US, C_Trim, C_Trim_Small, C_Variable |
| inductor | L, L_Small, L_45deg, L_Ferrite, L_Ferrite_Small, L_Iron, L_Iron_Small, L_Polarized, L_Trim |
| diode | D, D_Small, D_45deg, D_45deg_Filled, D_Filled, D_Small_Filled |
| led | LED, LED_Small, LED_45deg, LED_45deg_Filled, LED_Filled, LED_Small_Filled |

As variantes variáveis/trim usam o valor configurado; não ganham automaticamente
um controle mecânico. Termistores usam a resistência configurada em um ponto
fixo, conforme o escopo atual sem temperatura. Não há curva NTC/PTC ou
autoaquecimento. Os indutores continuam lineares, sem saturação do núcleo.

`Simulation_SPICE:D` usa o diodo genérico e importa os parâmetros literais
`is`, `n`, `rs`, `bv`, `cjo` e `tt`. Isso preserva o padrão `rs=50m cjo=10p`
que vem nesse símbolo. Uma peça conhecida escrita em Value, como `1N4148`,
continua selecionando o modelo específico, conferindo A/K. Parâmetros manuais
do CA têm prioridade sobre os valores importados. O editor visual conserva
essa importação no componente ao expor separadamente a junção e sua
resistência série.

Não foram ligados automaticamente fotoresistores, cristais, fusíveis, DIACs,
diodos de túnel, LEDs em série ou outros dispositivos com funções diferentes
apenas porque possuem dois terminais.

## Pacotes resistivos e divisores

Os 32 desenhos `R_Pack02` a `R_Pack11`, suas variantes `_Split`, e os
`R_Pack02_SIP` a `R_Pack07_SIP`/`_SIP_Split` passaram a usar combinações de
resistores **isolados**. Value define a resistência de cada elemento.
No pacote com filas opostas, o par i é i/2N+1−i; no SIP são pares adjacentes
1–2, 3–4 etc. Eles não usam os modelos de rede com um terminal comum.

`R_Shunt` e `R_Shunt_US` têm um resistor entre os terminais de força 1/4 e
terminais de leitura Kelvin 2/3. As ligações internas de leitura e dos botões com terminais duplicados usam
straps de 1 µΩ. Assim também funcionam quando os pinos já estão unidos na
mesma rede do esquema, sem laços de fontes ideais de zero volts. Resistências
reais de fios e outros parasitas não são inferidos.

Os três divisores fixos têm resistores superior/inferior configuráveis,
consideram a carga externa e respeitam o número do tap:

| Símbolo | Superior / tap / inferior |
|---|---|
| VoltageDivider | 1 / 2 / 3 |
| VoltageDivider_CenterPin1 | 2 / 1 / 3 |
| VoltageDivider_CenterPin3 | 1 / 3 / 2 |

Esses divisores usam `upper_resistance` e `lower_resistance`, ambos inicialmente
10 kΩ, em vez de interpretar Value como a resistência de ambos.

## Verificação e instalação

A auditoria completa encontra **617 entradas eletrônicas vinculadas**, sem
ambiguidade ou incompatibilidade de números de pinos. A validação separada
confere também os nomes A/K/B/C/E/G/D/S quando aplicáveis. Existe um ID legado
`74xx:74HC125` que não consta desta biblioteca instalada; ele é conservado para
projetos anteriores e não é contado como vínculo padrão disponível.

As novas bancadas estão em `qa/tests/spice/test_standard_symbol_associations.cpp`.
Elas usam uma seleção isolada dos arquivos distribuídos, sem perfis pessoais,
CA.Model, CA.Pins ou CA.Parameters gravados previamente. Conferem estados
iniciais, comutação, contatos comuns, canais isolados, controles DIP independentes,
LED separado do contato, pinos duplicados separados/empilhados, resistência por
Value, ligações Kelvin separadas/unidas, divisores
carregados e a prioridade dos parâmetros/peças do diodo SPICE.

```bash
python3 tools/circuitalive/model-catalog.py --fail-on-errors
python3 -m unittest discover -s tools/circuitalive/tests -v
tools/circuitalive/build.sh --target qa_interactive_sim
tools/circuitalive/test-interactive.sh
tools/circuitalive/install.sh
```

O relatório completo fica em `.circuitalive/reports/model-catalog.csv` e `.json`.
O relatório específico desta revisão é
`.circuitalive/reports/standard-association-added.csv`. São relatórios derivados;
a simulação consulta os próprios `.casim`. A instalação não depende desses
relatórios nem de uma pasta de preferências do desenvolvedor.

Resultado desta revisão: **310 casos nativos aprovados** na suíte completa
(188,58 s), seis testes Python aprovados e nenhuma ambiguidade/incompatibilidade
na auditoria. As 12 bancadas novas também passaram lendo exclusivamente a
pasta instalada, com 1.645 verificações. Os 396 arquivos instalados foram
comparados com os fontes, e o editor instalado corresponde à compilação atual.
