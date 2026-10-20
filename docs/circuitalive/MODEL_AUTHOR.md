# Editor visual de modelos de simulação

Na janela principal do CircuitAliveEDA, clique no ícone **Simulation Model Editor**,
logo abaixo de **Symbol Editor**. A ferramenta abre uma janela própria; não é
necessário abrir um esquemático para criar o arquivo de modelo.

## Criar um modelo

1. Clique em **Choose symbol...** e escolha um componente da biblioteca normal.
   Todos os pinos físicos são importados, inclusive os das outras unidades de um CI.
2. Defina **Name** e **Model ID**. O ID aceita letras, números e `_`.
3. Na aba **Build behavior**, use **Add block...** para adicionar resistores,
   capacitores, comparadores, portas lógicas, latch, flip-flop, estágios de saída
   e outros modelos disponíveis.
4. Ligue os terminais clicando primeiro em um e depois no outro. Os pinos físicos
   do componente ficam no painel à esquerda. Arraste os blocos para organizá-los;
   **Zoom +**, **Zoom -** e **Fit** ajustam a visualização.
5. Dê dois cliques em um bloco para configurar seus valores e conexões. A coluna
   **Connected to** também permite selecionar um pino externo (`@nome`) ou digitar
   um nome de ligação interna. O mesmo nome interliga os terminais. Campo vazio
   deixa o terminal aberto.
   Dois cliques com o botão direito excluem o bloco ou fio apontado. Ao apagar
   um fio, a ligação elétrica é desfeita e os outros ramos permanecem conectados.
6. Clique em **Save** e escolha um arquivo `.casim`, preferencialmente na pasta
   `models/` do seu projeto.

Com **Use this model for new symbols of the selected component** marcado, salvar
também guarda a associação pelo ID exato de biblioteca, como `Device:R`. Novas
instâncias desse símbolo recebem o arquivo e o mapa de pinos. As instâncias que
já estavam no esquema conservam suas configurações; nelas, use **Simulation
model... → Load file...** para escolher o modelo criado.

Esse padrão é pessoal, armazenado no perfil de modelos do CircuitAliveEDA. Ele
aponta para o caminho absoluto do arquivo salvo. Ao mover o arquivo, atualize a
associação. Para compartilhar um projeto, mantenha seus arquivos de modelo junto
dele e confira os caminhos na configuração dos símbolos.

## Parâmetros que aparecem no componente

A aba **Parameters** define os parâmetros públicos: ID, nome, padrão, unidade e
limites. Por exemplo, crie `resistance`, com padrão `1k`, unidade `Ohm`, mínimo
`1u` e máximo `1T`. Na propriedade Resistance do bloco resistor, selecione ou
digite `@resistance`. O usuário poderá ajustar esse parâmetro na janela
**Simulation model...** de cada instância do componente.

Valores fixos, como `10k`, `100n`, `5V` e `3mA`, também são aceitos nos blocos.
O valor especial `$value` acompanha o campo Value do símbolo. Os limites do
parâmetro público devem caber nos limites do parâmetro do bloco associado.

O mapa da aba **Component pins** relaciona número físico, ID lógico, nome e função
elétrica. Os blocos se conectam pelo ID lógico; ao renomeá-lo, atualize suas
ligações. Alimentação e referência devem apontar para os pinos corretos do CI.

## Reaproveitar um modelo

**Start from model...** duplica um modelo existente para alterar pinagem e
parâmetros ou acrescentar blocos. O núcleo SPICE/comportamental de um arquivo
antigo é preservado como um bloco interno; ele não é convertido automaticamente
em um diagrama. Modelos criados neste editor reabrem com os blocos, valores,
posições e conexões editáveis.

O catálogo de blocos combina os modelos do projeto, os modelos distribuídos e os
blocos de `simulation/blocks/`. Assim, um modelo visual salvo em `models/` também
pode ser usado como bloco de outro modelo. O arquivo final incorpora os blocos
utilizados: não depende dos arquivos originais para simular ou reabrir o diagrama.
Editar um bloco original posteriormente não altera suas cópias já incorporadas.

## Consumo de alimentação

Para representar consumo de alimentação em qualquer modelo, adicione **Load**
em **Add block...** (categoria **Building blocks / Loads**). Conecte
**Supply (+)** ao VCC e **Return (-)** ao GND; configure **Consumed current**,
por exemplo `20m` para 20 mA. Esse bloco ideal representa corrente constante e
aceita valores a partir de zero. O consumo se soma aos demais caminhos elétricos
do modelo; ele também aceita referências numéricas como `#slider1` e `#button1`.
O bloco geral **Constant current** continua disponível para modelos anteriores.

## Chaves e condições ideais sem alimentação

Em **Add block... → Building blocks → Ideal primitives**, os blocos abaixo não
exigem pinos de alimentação. Suas entradas de controle usam tensão em relação
ao terra do circuito. O limiar padrão dos controles é `2.5 V`; os comparadores
e portas produzem `5 V` para verdadeiro e `0 V` para falso, com níveis ajustáveis.

| Bloco | Comportamento |
|---|---|
| **Analog switch NO** | Fecha o contato bidirecional A–B quando Enable ≥ threshold; abre abaixo do limiar. |
| **Analog switch NC** | Fecha A–B quando Enable < threshold; abre a partir do limiar. |
| **Analog selector SPDT** | Liga Common Y a A quando Select é verdadeiro, ou a B quando falso. |
| **IF / ELSE** | Faz a tensão em Y acompanhar A quando Condition é verdadeiro, ou B quando falso. |
| **Analog buffer with Enable / Hi-Z** | Copia a tensão analógica de A para Y quando habilitado; deixa Y em alta impedância quando desabilitado. |
| **Voltage window / supply valid** | Produz verdadeiro se a tensão diferencial Plus–Minus estiver entre Minimum e Maximum, incluindo os limites. |
| **COMPARE A ≥ B**, **A < B**, **A ≤ B** | Compara as tensões das duas entradas. O bloco **CONDITION / comparator** existente cobre A > B. |
| **COMPARE A = B**, **A != B** | Compara a diferença absoluta com **Equality tolerance**, padrão `1m V`. |
| **NAND**, **NOR**, **XOR**, **XNOR** | Completam as portas ideais AND, OR, BUFFER e INVERTER existentes. |

As chaves NO/NC e SPDT conduzem corrente nos dois sentidos pelos contatos e
preservam tensões intermediárias ou negativas; não geram a tensão do sinal.
**Closed resistance** e **Open resistance** aproximam os estados fechado/aberto
com valores finitos para permitir a resolução numérica do circuito.

**IF / ELSE** é um seletor de valores de tensão com saída ativa; não isola a
saída nem conecta fisicamente seus dois sinais de entrada. O buffer habilitável
também tem saída ativa, mas abre seu caminho de saída quando desabilitado.
Portas, comparadores, IF e buffer obtêm energia de fontes ideais internas;
consumo de alimentação não é representado nesses blocos.

Os campos numéricos desses blocos aceitam valores fixos, `@parâmetro` público
e `#controle` compatível com seus limites. Para encadear condições, conecte
suas saídas às entradas das portas ideais e depois a **Enable**, **Select** ou
**Condition**.

### Saída analógica somente com alimentação válida

Para um sensor de 5 V com slider:

1. No bloco do slider, mude **Output** da ligação direta ao OUT para uma ligação
   interna, por exemplo `value`. Mantenha **Return** no GND.
2. Adicione **Voltage window / supply valid**. Ligue **Measured voltage (+)** ao
   VCC, **Measured reference (-)** ao GND e **Output Y** a uma ligação `power_ok`.
   Configure **Minimum** = `4.5` e **Maximum** = `5.5`.
3. Adicione **Analog switch NO**. Ligue **Contact A** a `value`, **Contact B** ao
   OUT e **Enable** a `power_ok`. O limiar padrão de `2.5` combina com os níveis
   `0/5 V` do detector.
4. Com alimentação válida, OUT acompanha o slider. Fora da faixa, o contato abre.
   Um resistor externo de pull-down, por exemplo `100k`, pode definir zero quando
   a saída está aberta e há um GND de referência conectado.

O detector mede diferença de potencial; ele não verifica a presença física de
um fio. Pinos desconectados podem ficar flutuando ou receber tensão por outros
caminhos. Saída em alta impedância significa que o bloco deixou de acioná-la;
uma carga externa ainda pode definir sua tensão. A faixa de alimentação também
não limita o slider: configure seus limites de saída separadamente.

## Exemplo do 555

**555 example** abre uma composição pronta, associada a `Timer:NE555P`, com:

- Três resistores formando a referência de 1/3 e 2/3 da alimentação.
- Comparadores de trigger, threshold e reset.
- Latch com prioridade do reset externo.
- Estágio de saída com resistência e limite de corrente.
- Transistor de descarga e consumo de alimentação.

Os parâmetros principais ficam expostos na aba **Parameters**. O exemplo pode
ser salvo, alterado e associado ao componente. A validação usa uma montagem
astável com RA = RB = 10 kΩ e C = 1 µF: o capacitor oscila perto de 1/3 e 2/3
de VCC, com período próximo de 20,8 ms; reset baixo força a saída baixa.

Trata-se de um modelo comportamental com efeitos elétricos selecionados. A
precisão de um componente criado depende dos blocos, parâmetros e testes usados
para representar seu datasheet. Microcontroladores ainda exigem uma futura
camada de execução de firmware; desenhar os pinos e ligar portas não implementa
essa camada.

## Testar na própria ferramenta

Na aba **Test model**, configure por pino:

- `5V` ou `0V`: fonte ideal em relação ao terra da bancada.
- `load:10k`: carga resistiva para o terra.
- Vazio ou `open`: terminal aberto.

**Run test** roda uma sessão isolada de ngspice e mostra a tensão final de cada
pino. **Duration** aceita, por exemplo, `20m` para 20 ms, até 1 segundo. **Symbol
Value** fornece o valor usado por parâmetros `$value`. A bancada não altera o
modelo salvo. Para circuitos externos com capacitores, clocks, realimentação e
formas de onda, associe o modelo no esquemático e use as probes e o osciloscópio.

Salvar ou editar `.casim` não exige recompilar. Use **Stop** e depois **Play** para
carregar o arquivo atualizado em uma nova sessão.

## Controles visuais durante a simulação

A aba **Interactive controls** configura os controles que aparecem junto do
componente no esquemático durante Play/Pause. Cada instância mantém seu próprio
estado; os controles só aparecem na página à qual o componente pertence.

- **Expose block control...** reaproveita um botão ou ajuste de um bloco interno,
  como uma chave ou o cursor de um potenciômetro.
- **Add slider input** cria um bloco de entrada ajustável e seu slider.
- **Add button input** cria um bloco de entrada que alterna entre OFF e ON por clique.
- **Edit control...** altera rótulo, valores, limites, unidade e área clicável.
  **Remove control** remove a interação visual; o bloco elétrico permanece no circuito.

Sliders e botões no modo **Voltage input** têm **Output** e **Return**. Ligue esses
terminais na aba **Build behavior**: a tensão entre eles representa o valor escolhido. Por exemplo,
um slider de luminosidade pode produzir 0...1 V para um comparador, com um limiar
de 0,5 V. O rótulo pode ser “Luz recebida”; a unidade `%` mostra 0...1 como
0...100%. Outras unidades são rótulos: a relação com a grandeza física depende
do circuito construído. O botão nesse modo pode fornecer, por exemplo, 0 V e 5 V.
No modo **Switch**, padrão dos novos botões, os terminais são **Input** e **Output**:
ON fecha o contato entre eles e OFF o abre.

O alvo **Whole symbol** usa o contorno do componente inteiro. **Positioned rectangle**
permite áreas independentes dentro ou fora dele. Arraste o retângulo na prévia
do símbolo para posicionar; arraste suas bordas ou cantos para redimensionar.
Os campos de largura e altura também permitem definir medidas exatas.
Posições e tamanhos são proporcionais ao corpo exibido do símbolo; valores negativos
posicionam o controle acima ou à esquerda. Em componentes com vários controles,
use retângulos separados. Um retângulo explícito tem prioridade sobre o alvo
do símbolo inteiro.

Marque **Hide outline/text during simulation; keep clicks active** para ocultar
a borda e o texto durante a execução. A mesma área continua recebendo cliques.
Na prévia do editor ela permanece visível para poder ser posicionada e redimensionada.

Na execução, clicar no retângulo de slider abre o ajuste correspondente. Botões
alternam o estado a cada clique. Mudanças entram no circuito ativo sem reiniciar
a aquisição ou alterar o arquivo do esquemático. Stop/Play inicia novamente nos
valores configurados no modelo. A configuração e o posicionamento ficam no
`.casim`, junto com os blocos incorporados, e reabrem no editor de modelos.

### Usar um controle no valor de outro bloco

Cada controle tem um **Reference name**, como `slider1` ou `button1`. Nos valores
dos blocos, escreva `#slider1` para usar o valor atual do slider. Um mesmo controle
pode alimentar vários campos, sem desenhar fios para essa ligação. Os nomes
disponíveis aparecem no seletor de valores das propriedades do bloco.

Exemplo: adicione **Ideal resistor**, configure um slider com mínimo `100`, máximo
`10k`, passo `100` e inicial `1k`. No campo **Resistance** do resistor, coloque
`#slider1`. Durante Play/Pause, o ajuste altera a resistência dessa instância;
o circuito e os estados dos capacitores continuam em execução.

Também é aceito o **Label** como referência: “Resistance” pode ser usado como
`#Resistance`. Espaços viram `_` e letras ASCII ignoram maiúsculas/minúsculas.
Labels repetidos precisam de nomes de referência distintos. Prefira o nome
próprio, que permanece estável ao alterar o texto exibido.

Nos botões, **#reference value when ON/OFF** define os dois valores fornecidos
pela referência, independentemente da função elétrica dos contatos. Por exemplo,
`1k` e `10k` permitem colocar `#button1` na resistência de um bloco. Em um destino
numérico, os dois valores precisam ser números SI válidos dentro dos limites do
parâmetro. Textos como “LIGADO” e “DESLIGADO” são aceitos em destinos de texto:
colocar `#button1` no **Label** de outro controle faz seu rótulo acompanhar o botão.
O mesmo vale para **Display unit**, que é um texto de apresentação.

As referências numéricas cobrem campos elétricos mutáveis de resistores,
capacitores, indutores, fontes, parâmetros de modelos nativos e expressões
comportamentais. Configurações que exigem reconstruir o circuito, parâmetros
globais de resolução e determinados ajustes de apresentação/detecção são
recusados ao salvar. Não é permitido criar uma referência circular no valor
inicial de um controle. O intervalo completo do slider deve caber nos limites
do parâmetro de destino; uma resistência, por exemplo, precisa permanecer positiva.

### Botão como contato elétrico

O novo botão usa **Switch: ON passes, OFF opens**: ligue **Input** e **Output**
ao caminho que deseja abrir/fechar. **Closed resistance** e **Open resistance**
nas propriedades do bloco definem as resistências dos contatos em cada estado.
Para conservar o funcionamento de uma entrada de tensão, escolha **Voltage input:
ON / OFF levels**, que usa **Output/Return** e dois níveis configuráveis.

As duas formas podem também fornecer um `#button1`. Quando o controle serve
somente como referência de valores, seus terminais podem ficar sem fios.

## Organização do código

- `model_author.cpp`: janela, escolha do símbolo, desenho e bancada.
- `model_design.*`: documento, conexões, validação e composição dos modelos.
- `model_design_presets.*`: exemplos montados com os mesmos blocos públicos.
- `simulation/blocks/`: primitivas reutilizáveis carregadas em tempo de execução.
- `model_author_api.h`: interface privada entre o gerenciador e o módulo de
  esquemáticos, usando o mecanismo `IfaceOrAddress` existente do KiCad.

O gerenciador recebeu uma ação e uma entrada de launcher. O motor continua
usando o mesmo carregador `.casim`, netlist ngspice e processo de simulação.
O arquivo contém a seção elétrica e a seção `authoring` com a montagem editável.
Não é necessário registrar componentes novos no código C++.
