# Componentes usados

O painel **Used Components**, em **View > Panels**, lista os componentes da
página atual. Arraste sua barra de título para movê-lo, deixá-lo flutuante ou
encaixá-lo na tela. O botão **Float / dock** também alterna entre encaixado e
flutuante, como no painel de instrumentos. A posição e a visibilidade são
mantidas globalmente entre projetos, inclusive quando outro projeto é aberto
antes de fechar o editor atual. A geometria antiga associada ao arquivo aberto
não substitui essas preferências.

Arraste a divisória horizontal abaixo da prévia para ajustar a altura do símbolo
em relação à lista. Essa altura também é uma preferência global.

A lista acompanha inserções, exclusões, edições, desfazer e troca de página.
Cada símbolo de biblioteca aparece uma única vez, sem contador: valores,
modelos, footprints e unidades diferentes não criam outras entradas. A inserção
usa os campos iniciais da biblioteca e a configuração escolhida no seletor,
incluindo os padrões de simulação salvos. Alterar Value, Footprint ou campos de
um componente já colocado não muda os próximos componentes da lista rápida.
Ao abrir um esquema existente, os padrões da biblioteca são usados como base.
Labels, símbolos de
alimentação e sondas de simulação não entram na lista.

1. Selecione um componente na lista para ver seu símbolo na prévia superior.
2. Clique em uma área vazia do esquema para trazer a cópia ao cursor.
3. Posicione e clique novamente para inserir a unidade.

Para componentes com várias unidades, a lista começa na unidade A e percorre
todas as unidades do mesmo encapsulamento, incluindo a alimentação. Por exemplo:
**U2:A → U2:B → U2:C → U3:A** no 74LS139. Cada unidade usa o mesmo ciclo de dois
cliques. A referência só muda depois da última unidade; a continuação não depende
da opção de colocar todas as unidades na janela de escolha de símbolos.

Depois da inserção, o símbolo sai do cursor. A linha continua selecionada e
identifica o tipo ativo, inclusive ao passar para a próxima unidade. A escolha
na lista continua ativa:
cada novo par de cliques em espaço vazio permite inserir outra cópia. Clicar em
um componente existente o seleciona, inclusive durante a prévia de inserção,
sem criar uma cópia por cima. Com um símbolo no cursor, **Esc** cancela apenas a
prévia, mantendo a escolha para inserir novamente. **Esc** sem prévia desativa
a escolha e volta ao ponteiro normal.
Trocar de ferramenta ou ocultar o painel também encerra essa escolha.

O mesmo [ciclo de inserção e repetição](PLACEMENT_WORKFLOW.md) atende agora os
itens escolhidos por atalho ou botão, incluindo alimentação, rótulos, texto e
objetos gráficos. Essas escolhas independem da visibilidade deste painel.

O painel e a inserção usam os mesmos mecanismos wxWidgets e ferramentas do
editor nas versões Windows e Linux.
