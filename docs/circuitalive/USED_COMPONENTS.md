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
modelos, footprints e unidades diferentes não criam outras entradas. A cópia
herda as propriedades de uma ocorrência desse tipo na página. Labels, símbolos de
alimentação e sondas de simulação não entram na lista.

1. Selecione um componente na lista para ver seu símbolo na prévia superior.
2. Clique em uma área vazia do esquema para trazer a cópia ao cursor.
3. Posicione e clique novamente para inserir uma única cópia, com referência nova.

Depois da inserção, o símbolo sai do cursor. A escolha na lista continua ativa:
cada novo par de cliques em espaço vazio permite inserir outra cópia. Clicar em
um componente existente o seleciona, inclusive durante a prévia de inserção,
sem criar uma cópia por cima. Com um símbolo no cursor, **Esc** cancela apenas a
prévia, mantendo a escolha para inserir novamente. **Esc** sem prévia desativa
a escolha e volta ao ponteiro normal.
Ocultar o painel também encerra essa escolha.

O painel e a inserção usam os mesmos mecanismos wxWidgets e ferramentas do
editor nas versões Windows e Linux.
