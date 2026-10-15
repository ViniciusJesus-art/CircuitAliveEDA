# Fork do KiCad — mapa inicial da simulação interativa

Análise de código em 15/10/2026. Projeto de Vinicius Dias.

## Resultado

O KiCad fornece uma base concreta para desenvolver simulação interativa diretamente no esquemático: conectividade, geração de netlist, modelos SPICE, integração com ngspice, ferramentas de seleção e renderização. Ainda precisamos implementar a sessão contínua, o fluxo de resultados em tempo de execução, os indicadores e a biblioteca de modelos automáticos.

Este documento distingue **código existente verificado** de **arquitetura proposta**. A análise foi estática: não compilei o KiCad, não executei simulações e não criei um fork remoto nem alterei os fontes.

## Base examinada

- Repositório oficial: https://gitlab.com/kicad/code/kicad
- Espelho consultado e clonado: https://github.com/KiCad/kicad-source-mirror
- Branch: `10.0`.
- Commit fixado: `6809ada8746084720c0093eb484c889afb35d0d4`.
- Link da revisão: https://github.com/KiCad/kicad-source-mirror/tree/6809ada8746084720c0093eb484c889afb35d0d4
- Cópia local de análise: `/workspace/scratch/6a2a0a29d102/kicad-source`. Checkout parcial, adequado à inspeção realizada; precisa ser expandido para uma compilação completa.

Os caminhos e localizadores abaixo se referem exclusivamente a essa revisão. A branch pode mudar.

## Requisitos recuperados

O chat “Projeto Proteus Aberto” foi consultado por recuperação de contexto. Foi possível recuperar seus requisitos principais, mas não uma transcrição integral:

- Linux/Fedora como plataforma inicial.
- Editor profissional e integração esquemático/PCB do KiCad.
- Play, Pause e Stop na barra superior do esquemático.
- Execução contínua e interação direta com componentes.
- Indicadores quadrados de estados lógicos nos pinos.
- LEDs visíveis respondendo ao circuito.
- Simulação analógica e digital juntas; transistores, relés, famílias 74HC/74AC e memórias no escopo progressivo.
- Associação automática de modelos para os componentes suportados, evitando uma configuração manual repetitiva.

A sugestão anterior de ngspice/XSPICE é uma base a validar por protótipos; não demonstra, por si só, desempenho suficiente para qualquer circuito complexo.

## Mapa dos arquivos existentes

Todos os caminhos desta tabela são relativos à raiz do KiCad.

| Área | Arquivos | O que foi verificado e uso no projeto |
|---|---|---|
| Barra superior | `eeschema/toolbars_sch_editor.cpp` | O bloco `TOOLBAR_LOC::TOP_MAIN` monta as ações do editor. Local direto para adicionar os três controles. Considerar também configurações de toolbar já personalizadas pelo usuário. |
| Declaração de comandos | `eeschema/tools/sch_actions.h`, `sch_actions.cpp` | Definem `TOOL_ACTION`, incluindo `showSimulator`, `simProbe` e `simTune`. Novas ações precisam de nomes próprios para a sessão interativa. |
| Registro e vida do editor | `eeschema/sch_edit_frame.h`, `sch_edit_frame.cpp` | `setupTools()` registra ferramentas; o frame também controla fechamento e troca de contexto. Lugar para vincular e encerrar a nova sessão. |
| Entrada atual da simulação | `eeschema/tools/sch_inspection_tool.cpp` | `RunSimulation()` abre `FRAME_SIMULATOR`; sua transição atende `showSimulator`. Mostra por que o botão atual abre outra janela. |
| Interação no esquemático | `eeschema/tools/sch_editor_control.h`, `sch_editor_control.cpp` | `SimProbe()`, `SimTune()` e `setTransitions()` dão exemplos de seleção e interação com componentes e pinos. Referência para uma ferramenta própria de chaves e botões. |
| Janela atual do simulador | `eeschema/sim/simulator_frame.h`, `simulator_frame.cpp` | Carrega o circuito e executa análises; `StartSimulation()`, `LoadSimulator()`, `onSimStarted()` e `onSimFinished()` são referências. Tem ponte com `wxQueueEvent` para devolver eventos à UI. |
| Comandos da janela atual | `eeschema/tools/simulator_control.h`, `simulator_control.cpp` | A classe trabalha com `SIMULATOR_FRAME`. Não é um controlador pronto para o editor esquemático, apesar do nome. |
| Interface do motor | `eeschema/sim/simulator.h`, `spice_simulator.h`, `spice_simulator.cpp` | Existe `Run/Stop/IsRunning/Clean` e listener de estado; `SIM_STATE` só distingue idle/running. Precisaremos de semântica explícita de pausa, retomada e reset. |
| Integração ngspice | `eeschema/sim/ngspice.h`, `ngspice.cpp` | Carrega a biblioteca, circuito e code models; `Run()` usa `bg_run`, `Stop()` usa `bg_halt`. Ponto central do fluxo contínuo de dados e do controle do motor. |
| Circuito para simulação | `eeschema/sim/spice_circuit_model.h`, `spice_circuit_model.cpp` | `SPICE_CIRCUIT_MODEL` combina exportador SPICE e `SIMULATION_MODEL`; fornece comandos e netlist. Reutilizar sua geração de circuito. |
| Esquema → netlist | `eeschema/netlist_exporters/netlist_exporter_spice.h`, `netlist_exporter_spice.cpp` | `ReadSchematicAndLibraries()` percorre folhas e símbolos, resolve modelos, números de pinos e nets. Respeita exclusões e possui tratamento de unidades. |
| Itens e nomes do motor | `eeschema/sim/spice_generator.h`, `spice_generator.cpp` | `SPICE_ITEM` guarda referência, números de pino, nomes de nets e modelo. `SPICE_GENERATOR` gera linhas e nomes de sinais/correntes. É parte do mapeamento de resultados para o desenho. |
| Grafo elétrico | `eeschema/connection_graph.h`, `connection_graph.cpp` | `Recalculate()`, `GetNetMap()` e consultas de subgrafos oferecem a conectividade existente. Reutilizar esse grafo; não deduzir conexão pela aparência dos fios. |
| Associação de modelos | `eeschema/sim/sim_lib_mgr.h`, `sim_lib_mgr.cpp` | `CreateModel()` recebe símbolo, folha e campos. Usa inferência para RLC/fontes. Bom ponto para integrar o catálogo automático antes da resolução final. |
| Metadados e modelos | `eeschema/sim/sim_model.h`, `sim_model.cpp`, `sim_model_serializer.cpp` | Campos existentes como `Sim.Device`, `Sim.Params` e `Sim.Pins`, criação e inferência de modelos. Permitem aproveitar configurações explícitas já feitas pelo usuário. |
| Modelos analógicos | `eeschema/sim/sim_model_ngspice.*`, `sim_model_subckt.*`, `sim_model_switch.*`, `sim_model_source.*` | Infraestrutura para dispositivos, subcircuitos, chaves e fontes. A física dos dispositivos depende dos modelos e de seus parâmetros. |
| XSPICE | `eeschema/sim/sim_model_xspice.h`, `sim_model_xspice.cpp`, `sim_xspice_parser.h` | Existe suporte à representação de code models e sintaxe de portas. A implementação de `SIM_MODEL_XSPICE` é pequena; não é um simulador digital completo dentro do KiCad. |
| Configuração manual existente | `eeschema/dialogs/dialog_sim_model.*` | Editor de modelos. Manter como opção avançada e resolução de componentes fora do catálogo. Arquivos `*_base.*` são gerados; suas telas de origem são `.fbp`. |
| Renderização | `eeschema/sch_painter.h`, `sch_painter.cpp` | Desenha símbolos/pinos e resultados OP. `draw(SCH_PIN*)` e `draw(SCH_SYMBOL*)` precisam ser considerados juntos: o símbolo usa pinos temporários para desenho. |
| Canvas e camadas | `eeschema/sch_draw_panel.cpp`, `sch_view.h`, `sch_view.cpp`, `include/layer_ids.h` | Já existem camadas de overlay para tensões/correntes OP. Referência para uma camada própria de simulação, com repintura desacoplada do cálculo. |
| Identidade e geometria | `eeschema/sch_symbol.h`, `sch_pin.h`, `sch_sheet_path.h` | Pinos, instâncias e posições; `GetPinPhysicalPosition()` trata a posição do pino no símbolo. Incluir orientação e caminho hierárquico no mapeamento. |
| Configuração | `eeschema/sim/spice_settings.*` | Configurações atuais do simulador; avaliar quais são compartilhadas e quais pertencem somente ao novo modo. |
| Compilação | `CMakeLists.txt`, `eeschema/CMakeLists.txt` | A revisão usa C++20; os arquivos de simulação são enumerados no CMake do Eeschema. Adicionar ali os novos módulos. |
| Testes | `qa/tests/spice/`, `qa/tests/eeschema/` | Existe alvo `qa_spice`, com testes de netlist, inferência, modelos e regressões. Reutilizar dados de `qa/data/eeschema/spice_netlists/`. |

### Localizadores especialmente úteis

Na revisão analisada:

- `ngspice.cpp:349`: início de `NGSPICE::Run()`.
- `ngspice.cpp:360`: início de `NGSPICE::Stop()`.
- `ngspice.cpp:579`: registro em `m_ngSpice_Init`; os argumentos de `SendData` e `SendInitData` estão como `nullptr`.
- `spice_simulator.h`: enum `SIM_STATE` e listener.
- `netlist_exporter_spice.cpp:136`: `ReadSchematicAndLibraries()`.
- `sch_edit_frame.cpp:1626`: `RefreshOperatingPointDisplay()`.
- `sch_painter.cpp:909`: desenho de pinos; `:2676`: desenho de símbolos.
- `sch_draw_panel.cpp:172`: configuração da camada de tensão OP como overlay.
- `sim_lib_mgr.cpp:202`: criação de modelo a partir de símbolo e folha.

O código de OP é uma referência, mas não deve ser chamado integralmente a cada passo: refaz resolução e percorre elementos que poderiam estar em cache.

## Arquitetura proposta

Criar um núcleo de sessão compartilhável, separado da janela de gráficos. A integração inicial pode usar o ngspice já carregado pelo KiCad, com um único proprietário ativo do motor por vez. O código existente contém inicialização estática: não presumir que duas sessões independentes funcionem ao mesmo tempo.

Fluxo:

1. Play pede ao editor um snapshot consistente do circuito.
2. A preparação resolve conectividade, modelos e pinagem e produz a netlist.
3. Um controlador inicia o motor e mantém tempo, comandos pendentes e estado.
4. O motor publica dados por callbacks em um buffer limitado.
5. A thread da UI consome snapshots numa frequência própria e atualiza o overlay.
6. Cliques em componentes viram eventos ordenados no tempo simulado.

O desenho nunca deve chamar o solver. Callbacks do motor também não devem acessar objetos wxWidgets ou ponteiros vivos do esquemático.

| Novos arquivos sugeridos — ainda inexistentes | Responsabilidade |
|---|---|
| `eeschema/sim/interactive_sim_session.h/.cpp` | Estado e ciclo de vida, Play/Pause/Stop, preparação, retomada e encerramento. |
| `eeschema/sim/interactive_sim_snapshot.h` | Valores, tempo, revisão da sessão e identificadores estáveis. |
| `eeschema/sim/interactive_sim_model_registry.h/.cpp` | Catálogo automático e seleção verificável de modelos/pinagem. |
| `eeschema/tools/sch_simulation_tool.h/.cpp` | Ações do editor e interação direta com chaves/botões. |
| `eeschema/sim/interactive_sim_overlay.h/.cpp` | Cache de estados visuais; integração com painter e camadas. |
| `qa/tests/spice/test_interactive_sim_*.cpp` | Verificação de estado, continuidade, modelos e resultados. |

Esses nomes são uma proposta de organização. Devem ser ajustados quando o primeiro protótipo demonstrar as fronteiras reais.

## Execução contínua e os três botões

| Ação | Contrato proposto |
|---|---|
| Play a partir de parado | Preparar circuito, resolver modelos e começar uma nova sessão. |
| Play a partir de pausado | Retomar o mesmo estado elétrico e tempo simulado. |
| Pause | Parar de avançar o tempo, preservando capacitores, flip-flops, memórias e último estado visual. |
| Stop | Encerrar e invalidar a sessão; o próximo Play começa nas condições iniciais definidas. |

A API documentada do ngspice oferece `bg_run`, `bg_halt` e `bg_resume`. O wrapper atual não separa Pause e Stop dessa forma. Alterações por comandos devem aguardar a parada confirmada do motor; os dados analógicos podem chegar por `SendData/SendInitData`, e eventos XSPICE por `ngSpice_Init_Evt`, conforme a versão instalada.

Ainda precisa ser provado: como prolongar a análise continuamente sem perder estado nem acumular histórico indefinidamente. Uma análise transiente possui horizonte; repetir análises desde zero não atende ao requisito. Comparar estratégias de pausa/continuação/extensão e memória em um protótipo pequeno antes de fixar a solução.

Distinguir tempo simulado, tempo de parede e frequência de desenho. Objetivo inicial: UI a aproximadamente 30–60 atualizações/s, com cálculo independente. Mostrar tempo simulado e razão de velocidade. Não prometer 1 segundo simulado por segundo real em circuitos grandes ou de MHz.

Um buffer limitado na UI não limita automaticamente os vetores internos do ngspice. Medir ambos; projetar seleção de sinais, retenção e descarte compatíveis com continuação.

## Estados lógicos e comportamento físico

- Exibir 0, 1 e Z como solicitado; acrescentar X para indeterminado/conflito.
- Separar estado de cada driver, valor resolvido da rede e leitura de cada entrada.
- Uma saída Z pode estar em uma rede puxada a 1. Nesse caso seu indicador de saída continua Z; a rede e as entradas podem ler 1.
- Não classificar Z por uma tensão intermediária.
- No XSPICE, estado lógico e força do driver são informações separadas. A normalização para a UI deve preservar essa distinção.
- A interpretação de uma tensão como 0/1/X depende da família, alimentação e modelo de entrada.
- Modelar saídas com impedância e capacidade de corrente adequadas. Uma porta lógica abstrata não oferece automaticamente o comportamento elétrico de um 74HC ou 74AC.
- Portas de um mesmo CI precisam compartilhar alimentação e estado do encapsulamento; o mapeamento deve contemplar múltiplas unidades e pinos ocultos.
- A fronteira digital/analógica precisa de modelos de entrada/saída apropriados, incluindo carga, atrasos e alta impedância. Validar se as pontes XSPICE disponíveis atendem ao perfil escolhido.
- Memórias exigem lógica de leitura/escrita, tempos, controle de barramento e carga de imagem. Isso não está resolvido pela existência de um símbolo SRAM no KiCad.

Para LEDs, utilizar corrente no ramo, polaridade e modelo do diodo para produzir brilho. Mapear explicitamente os nomes das correntes geradas pelo motor. Integrar pulsos no intervalo de exibição para que PWM e clocks rápidos não desapareçam por amostragem da tela. Sobrevelocidade/corrente excessiva podem gerar indicação de condição fora do modelo; simular dano físico requer modelagem adicional.

## Associação automática de modelos

O símbolo desenhado não contém necessariamente o comportamento completo do componente. Para o Play simples, precisamos de um catálogo com:

- ID de biblioteca e modelo reconhecido.
- Pinagem física → portas do modelo.
- Unidades e alimentação.
- Parâmetros e condições de validade.
- Modelo de comportamento e perfil elétrico.
- Fonte, versão e licença do modelo.

Dar prioridade à configuração explícita já existente; depois ao catálogo conhecido; depois à inferência básica suportada. Componente sem modelo deve ser identificado no desenho com explicação curta. Evitar substituição silenciosa por um modelo ideal.

Bibliotecas de símbolos e modelos não precisam residir todas no repositório principal do KiCad. Um catálogo separado pode evoluir junto do fork, mantendo IDs e versões reproduzíveis.

## Sequência de implementação

### Etapa 0 — base reproduzível

Criar o fork remoto, preservar upstream e uma branch de trabalho; usar a revisão escolhida como ponto de partida. Expandir o checkout e compilar o KiCad original no Fedora antes de mudanças.

Confirmar dependências e versão real de libngspice/XSPICE. O código analisado exige C++20 e CMake >= 3.22 em Linux. Não copiar opções antigas de build sem verificá-las: a documentação pública menciona `KICAD_SPICE`, mas essa opção não aparece no CMake raiz desta revisão; `KICAD_SPICE_QA` existe para os testes.

Entrega: editor original abre um projeto e uma análise existente funciona.

### Etapa 1 — prova do motor e da continuidade

Circuito elétrico pequeno, com RC, chave e LED. Registrar dados em execução; demonstrar pausa e retomada preservando tensão do capacitor, eventos ordenados e término limpo.

Critério decisivo: validar extensão de duração e uso de memória. Essa prova antecede a conclusão de que ngspice é suficiente para o modo contínuo completo.

### Etapa 2 — primeiro circuito integrado ao esquemático

Adicionar ações, toolbar e ferramenta; preparar o circuito já desenhado; mostrar tempo/estado e LED com brilho calculado. A chave funciona por clique e há Play/Pause/Stop reais.

Para o circuito suportado, nenhuma janela obrigatória de análise nem tempo final exigido do usuário. Parâmetros internos continuam necessários e são administrados pela sessão.

### Etapa 3 — lógica e barramento

Introduzir uma porta com perfil elétrico documentado, clock lento, indicadores e uma saída tri-state. Validar Z com pull-up, conflito entre saídas, entrada inválida, alimentação e resistor/LED na saída.

### Etapa 4 — sequenciais e memórias

Flip-flop, contador, registradores e depois SRAM/ROM. Validar reset, setup/hold conforme modelo, enable, leitura, escrita e retomada após pausa.

### Etapa 5 — escala e refinamento

Expandir famílias 74HC/74AC, relés, instrumentos e catálogo. Medir circuitos maiores antes de usar o Wall-8 como referência de desempenho.

Uma eventual separação em processo auxiliar pode isolar falhas do solver; deve ser decidida conforme as provas de estabilidade. Um motor digital próprio ou separado só deve ser considerado após medir as limitações da integração inicial, pois sincronização mista adiciona trabalho substancial.

## Verificação e riscos concretos

| Caso de verificação | Evidência esperada |
|---|---|
| LED com resistores diferentes | Correntes coerentes com o circuito e brilho correspondente. |
| Capacitor carregando, Pause e Play | Continuidade da tensão e do tempo; ausência de reinicialização involuntária. |
| Stop seguido de Play | Condições iniciais reproduzíveis. |
| Saída Z com pull-up | Driver em Z e rede em 1, sem confundir os dois. |
| Duas saídas opostas | Conflito detectado; resultados coerentes com os modelos elétricos. |
| CI com unidades separadas e folhas repetidas | Pinos e resultados associados à instância correta. |
| SRAM escreve, pausa e retoma | Dados preservados; controle de barramento correto. |
| Alteração estrutural do desenho | Sessão invalidada ou reconstruída de forma explícita, sem uso de ponteiros antigos. |
| Execução longa | Memória e filas medidas; estratégia de retenção demonstrada. |
| Alternar simulador atual e sessão interativa | Propriedade exclusiva do motor e ausência de estado cruzado. |

No primeiro ciclo, alterações estruturais no circuito devem encerrar/invalidate a sessão e exigir novo Play; cliques de chave são eventos da simulação. Essa regra torna o comportamento previsível enquanto a reconstrução dinâmica não existe.

A qualidade física dependerá do catálogo e do nível de abstração escolhido. O projeto pode reproduzir a experiência de uso desejada sem presumir acesso à arquitetura interna ou aos modelos proprietários do Proteus.

## Referências

- Código KiCad da revisão examinada: https://github.com/KiCad/kicad-source-mirror/tree/6809ada8746084720c0093eb484c889afb35d0d4
- Desenvolvimento e compilação: https://dev-docs.kicad.org/en/build/getting-started/
- ngspice como biblioteca: https://ngspice.sourceforge.io/shared.html
- Manual oficial ngspice, consultado nesta análise, seções de biblioteca compartilhada, transiente e modelos digitais: https://ngspice.sourceforge.io/docs/ngspice-manual.pdf

O documento registra o mapa e os critérios de início. Implementação, compilação e benchmarks continuam pendentes.
