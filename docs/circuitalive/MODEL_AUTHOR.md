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
