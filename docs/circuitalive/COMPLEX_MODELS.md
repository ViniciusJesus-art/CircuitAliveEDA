# Perfis de símbolos e componentes complexos

## Guardar uma configuração para os próximos componentes

Na janela **Simulation model…**, **Use these settings for new … symbols** vem
marcado. Ao confirmar uma configuração válida, o editor guarda o modelo, a
pinagem, os parâmetros e as seleções V/I/P para o ID completo da biblioteca.
Por exemplo, `Device:C_Small_US` e `Device:C` podem ter padrões diferentes.

Ao inserir um novo símbolo pelo seletor de componentes, essas opções são copiadas
para os campos ocultos `CA.Model`, `CA.Pins`, `CA.Parameters` e `CA.Measurements`.
O arquivo do esquema passa a conter a configuração, inclusive depois de ser
aberto em outro computador. Copiar/repetir um componente preserva sua configuração
normalmente. Esquemas existentes não são modificados pela criação de um perfil.

- Um valor explícito, como `22u` no parâmetro capacitance, é reutilizado.
- Parâmetro vazio usa o padrão do modelo. `$value` continua acompanhando o Value
  de cada símbolo; o perfil não transforma automaticamente o nome do desenho em
  um valor elétrico. Nesse caso defina, por exemplo, `100n` no Value do capacitor.
- Desmarcar a opção permite configurar somente a instância atual.
- **Forget saved default**, seguido de **OK**, remove o padrão do símbolo. Cancelar
  não grava nem remove o perfil. Os componentes já colocados conservam seus campos.
- Uma associação explícita que já veio no símbolo é preservada na colocação.
- Desfazer no esquema desfaz a edição/colocação, não a preferência do usuário.

Neste checkout, os perfis ficam em
`.circuitalive/config/circuitalive/model-profiles.json`. É um arquivo do usuário,
independente da versão do KiCad. Fora do launcher, usa `KICAD_CONFIG_HOME`, depois
`XDG_CONFIG_HOME/circuitaliveeda` ou `~/.config/circuitaliveeda`. A variável
`CIRCUITALIVE_PROFILE_FILE` permite indicar outro arquivo. Gravação por arquivo
temporário e rename, com lock entre processos, evita arquivos parcialmente escritos
ou a perda de entradas quando dois editores gravam ao mesmo tempo.

Modelos próprios escolhidos por caminho relativo são guardados com caminho absoluto
no perfil, para que o próximo projeto encontre o arquivo correto. Esse arquivo
continua sendo necessário: o perfil não copia seu conteúdo. Ao compartilhar um
projeto, leve os `.casim` e ajuste caminhos externos, ou mantenha-os em `models/`
dentro do projeto. A instância configurada no projeto continua independente do perfil.

## Duas formas de descrever o comportamento

O envelope `.casim` continua na versão 1, com `backend: ngspice`. Há duas formas
combináveis de construir o circuito interno:

1. `spice.instance` e `spice.definitions`: macromodelos analógicos/mistos, fontes
   comportamentais, transistores, subcircuitos e modelos XSPICE inline.
2. `behavioral`: blocos digitais reutilizáveis, compilados em um circuito XSPICE
   com interfaces elétricas para os pinos físicos. Não exige escrever todas as
   pontes ADC/DAC e saídas de alimentação manualmente.

O novo compilador está em `eeschema/circuitalive/behavioral_model.*`; o armazenamento
de perfis está em `model_profiles.*`. Nenhum deles depende de desenhos específicos.
Os arquivos dos componentes ficam em `simulation/models/`. Novos componentes
expressáveis pelos blocos ou pelo SPICE existente não exigem recompilar o editor.

## Contrato behavioral v1

Um exemplo do núcleo de um flip-flop é:

```json
"behavioral": {
  "version": 1,
  "supply": {"vcc": "vcc", "gnd": "gnd"},
  "blocks": [
    {"type": "dff", "data": "d", "clock": "clk",
     "set": "!pre", "reset": "!clr", "q": "stored", "nq": "inverted",
     "initial": 2}
  ],
  "outputs": {
    "q": {"signal": "stored"},
    "nq": {"signal": "inverted"}
  }
}
```

Isso é um trecho de um `.casim`, não o arquivo completo. Veja `74hc74.casim` para
um exemplo executável com pinos, parâmetros, duas unidades e associação ao símbolo.

Todo pino `input` ou `bidirectional` cria um sinal digital com o mesmo ID. Os
blocos criam sinais internos e `outputs` liga esses sinais às saídas físicas.
`!nome` inverte um sinal; `0` e `1` são constantes. Um sinal pode alimentar vários
blocos, inclusive realimentações. Cada sinal interno tem um único produtor;
referências inexistentes, tipos de bloco desconhecidos e saídas incompletas são erros.

| Bloco | Campos específicos |
|---|---|
| `and`, `nand`, `or`, `nor`, `xor`, `xnor` | `inputs` (lista), `out` |
| `buffer`, `inverter` | `inputs` (um sinal), `out` |
| `dff` | `data`, `clock`, `set`, `reset`, `q`, `nq`, `initial` |
| `sr_latch` | `s`, `r`, `enable`, `set`, `reset`, `q`, `nq`, `initial` |
| `ram` | `data_in`, `data_out`, `address` (listas LSB primeiro), `write`, `select` (lista de um sinal), `select_value`, `initial` |

`initial` aceita 0, 1 ou 2 (X). Set/reset são ativos em 1; use inversão para pinos
ativos em 0. O DFF captura na borda de subida. O latch SR retém com S=R=0 e produz
X com S=R=1. A RAM escreve em nível alto no sinal `write`. Cada saída física pode
ter `enable`, um sinal lógico ativo em 1; desabilitada, fica Z.

Os parâmetros elétricos comuns, declarados no próprio arquivo, são `vil`, `vih`
(limiares proporcionais a VCC–GND), `vmin`, `vmax`, `rise_delay`, `fall_delay`,
`transition`, `rout`, `cin`, `iq`, `high_drop` e `low_drop`. RAM acrescenta
`read_delay` e `powerup_delay` (intervalo para estabilizar controles após religar). Essas escolhas aparecem na janela de parâmetros por componente.

As interfaces medem a alimentação local, convertem tensões de entrada para eventos
digitais e aplicam saídas com resistência finita e corrente retirada da alimentação.
A queda nominal da saída é configurável. Alimentação inválida desabilita os drivers.
X é um estado interno distinto de Z, mesmo quando um resistor externo define a
tensão da rede. O código interno 0/0,5/1 independe dos limiares físicos da família.
A detecção de rede sem driver conserva as limitações descritas em
[BASIC_AND_DIGITAL_MODELS.md](BASIC_AND_DIGITAL_MODELS.md).

Os limites atuais são 1.024 blocos por modelo, até 16 bits de endereço e 32 bits de dados
por bloco RAM, com até 2 Mibit de armazenamento total por modelo. Cada RAM é uma matriz de estado do XSPICE, não milhares de portas
analógicas. O histórico de eventos permanece desabilitado (`.esave none`), e a
interface mantém o histórico limitado de medições. Os atrasos digitais geram eventos;
não se força um passo analógico de nanossegundos durante toda a simulação.

## Componentes entregues nesta etapa

| Modelo | Associação automática e comportamento |
|---|---|
| `ne555.casim` | `Timer:NE555D/P`: divisor interno 3×5k, pino CONT, comparadores de trigger/threshold, latch, prioridade do reset, descarga, saída com queda/resistência/limite suave de corrente e consumo |
| `lm317.casim`, `lm317_to3.casim` | LM317 TO-220, SOT-223, TO-252, TO-263 e TO3, com a pinagem correspondente: referência ajustável, corrente de ADJ, dropout, corrente máxima e resposta |
| `l7805`…`l7824` | L7805, L7806, L7808, L7809, L7812, L7815, L7818 e L7824: tensão fixa, dropout, limite de corrente, consumo e resposta |
| `74hc02`, `74hc04`, `74hc86` | NOR, inversor e XOR, todas as portas do encapsulamento |
| `74ls08`, `74ls32` | AND e OR, com aproximação elétrica TTL nominal a 5 V |
| `74hc74` | Dois flip-flops D, preset/clear assíncronos e saídas complementares |
| `sram_62256` | `Memory_RAM:CY62256-70PC`: 32K×8, endereços, CS/OE/WE, barramento bidirecional, estado inicial X e atraso de leitura |

O limite padrão de passo do 555 é 100 µs, reduzido para RC/32 nas redes passivas
ligadas a TRIG/THRESH. Quando não é possível identificar uma rede RC, aplica-se
o limite conservador de 10 µs. O campo Max step e o limite geral do simulador
continuam impondo limites menores; o integrador também pode reduzir cada passo
por precisão. Modelos podem declarar `timing_rc: {"ports": ["trig", "thresh"],
"fallback": 0.00001}` para usar essa seleção automática.

Continuam disponíveis os modelos analógicos básicos, HC00 NAND e HC125 tri-state.
A pinagem física foi conferida na biblioteca padrão instalada. Unidades diferentes
do mesmo CI compartilham uma única instância de simulação.

Exemplos para abrir no editor:

- `demos/simulation/circuitalive/ne555_astable.kicad_sch`: duas probes de osciloscópio,
  capacitor de temporização e medições de alimentação. RA=RB=10k, C=1u, VCC=9 V;
  período de cerca de 20,8 ms após a partida.
- `demos/simulation/circuitalive/linear_regulators.kicad_sch`: 7805 e LM317, com
  saída fixa de aproximadamente 5 V e ajustável de aproximadamente 5,04 V.

## Realismo, custo e extensões

São macromodelos comportamentais escritos para este projeto, com parâmetros
editáveis. Não são modelos certificados pelos fabricantes. Representam os efeitos
listados, sem reproduzir cada transistor. Reguladores não incluem temperatura,
dissipador, proteção térmica, área segura, ruído ou estabilidade de malha com todo
capacitor/ESR. O 555 não inclui dispersão térmica, transistores completos ou todas
as correntes de entrada. Os modelos LS usam limiares proporcionais calibrados em
5 V, não uma etapa de entrada TTL completa.

A RAM modela leitura e escrita, não todas as violações de setup/hold, tempos mínimos
de escrita ou degradação por baixa tensão. Não carrega imagens ROM/Flash nesta etapa.
Perda de alimentação invalida a matriz como X; os endereços ficam bloqueados
durante a recuperação para evitar uma escrita acidental. Não representa retenção
com bateria. Pause/Play conserva o estado; Stop/Play cria uma matriz nova. Microcontroladores com
firmware ainda precisam de um motor de CPU/periféricos e sincronização com o solver.
O vínculo símbolo/arquivo e o worker isolado podem ser reaproveitados, mas este
compilador de blocos não executa instruções de uma CPU ou arquivos HEX/ELF.

Para novos CIs: use composição de blocos para lógica e memória; use macromodelo
SPICE inline para comportamento analógico/misto. Um novo tipo de bloco ou motor
exige implementar e testar essa capacidade uma vez; depois os componentes que a
utilizam continuam sendo arquivos carregados em tempo de execução.

Referências de implementação: [manual oficial do ngspice, capítulos 5 e 8](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf),
[NE555](https://www.ti.com/product/NE555), [LM317](https://www.ti.com/product/LM317) e
[LM7800](https://www.ti.com/product/LM7800). Os arquivos não incorporam macromodelos
proprietários desses fabricantes.

## Verificação

- 63 casos em `qa_interactive_sim` e 66 em `qa_spice` aprovados.
- Perfis: reabertura, separação por ID, pinagem, parâmetros, `$value`, caminhos,
  medições, remoção e rejeição de armazenamento inválido; adaptação para campos
  nativos sem alterar instâncias já configuradas.
- Blocos: tabelas lógicas, retenção/reset do DFF, latch SR definido por arquivo e
  rejeição de tipos/sinais desconhecidos.
- RAM: escrita de A5 e 3C em endereços distintos, leitura, OE tri-state e invalidação
  do conteúdo por perda de alimentação seguida de recuperação.
- 555: oscilação astável, faixa do capacitor, período e reset.
- Reguladores: oito tensões, dropout, limitação de corrente, ajuste por divisor,
  corrente de entrada coerente com a carga/consumo e pinagem diferente do LM317 TO3.
- Esquemas nativos: captura/conexões dos exemplos 555 e reguladores, com medições
  e canais de osciloscópio.
- Interface em configuração/display isolados: perfil salvo no diálogo, capacitor
  novo da biblioteca herdando `builtin:capacitor` e `22u`; Cancelar preserva o perfil.

Logs locais: `.circuitalive/logs/test-complex-final-session.log`,
`test-complex-final-spice.log` e capturas `complex-profile-*.png`.

## Imagens e inspeção de memória

RAM e ROM agora podem declarar recursos e regiões inspecionáveis. Veja [MEMORY_IMAGES.md](MEMORY_IMAGES.md) para BIN, Intel HEX, snapshots com X, EPROM 27C512-55 e o contrato dos blocos `ram`/`rom`. Os limites de 16 bits/2 Mibit descritos acima continuam aplicáveis ao bloco legado sem `memory`; regiões novas seguem o limite agregado de 1 MiB do circuito.
