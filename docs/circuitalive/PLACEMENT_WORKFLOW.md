# Inserção e repetição no esquema

Acionar um atalho ou botão de inserção inicia uma nova escolha. Por exemplo,
**P** abre o seletor de alimentação, **A** abre o seletor de componentes e **L**
abre as propriedades do novo rótulo.

Depois da escolha:

1. O item aparece no cursor para posicionamento.
2. Um clique coloca o item e libera o cursor.
3. Outro clique em uma área vazia recupera o último item para posicionar.
4. O clique seguinte coloca a nova cópia e libera o cursor novamente.

Os cliques de repetição conservam a escolha e suas propriedades, sem abrir
novamente o seletor. Para escolher outro item, acione novamente o atalho ou
botão, inclusive durante uma prévia. Clicar em um objeto existente continua
permitindo selecioná-lo; Shift/Ctrl/Alt conservam as funções de seleção.

Esc durante uma repetição remove a prévia e conserva a escolha. Esc com o
cursor livre encerra essa escolha. Trocar de página ou variante também limpa
a escolha anterior. A repetição funciona com o painel de componentes usados
oculto; selecionar um componente nesse painel substitui a escolha anterior.

## Itens e conexões

O ciclo atende componentes, alimentação, rótulos locais/globais/hierárquicos
e diretivos, texto, imagens, formas, caixas de texto, tabelas, áreas, folhas
hierárquicas e conjuntos de itens importados. A criação inicial de objetos
dimensionados conserva os passos de desenho e configuração; as repetições
recuperam a geometria e o conteúdo já definidos.

Rótulos e alimentação podem ser repetidos diretamente com um clique sobre um
trecho livre de fio. Junções, marcas de não conexão, entradas de barramento e
sondas mantêm a inserção direta de suas ferramentas. Fios, linhas e barramentos
mantêm o desenho por pontos.

Ao habilitar **Place all units**, cada novo clique recupera a próxima unidade
livre do mesmo componente. As unidades conservam a referência, como U1A/U1B.
Ao terminar o encapsulamento, a próxima repetição começa outro componente.
Uma lista de rótulos segue sua sequência, também liberando o cursor a cada
colocação; cancelar uma prévia não pula seu rótulo.

As cópias recebem novos identificadores. Grupos mantêm seus membros e tabelas
mantêm suas células. Repetir uma folha cria outra instância do mesmo arquivo
hierárquico, com um nome de folha disponível. Cada colocação é uma operação de
desfazer. A escolha usa uma cópia independente e sobrevive à remoção do item
original.

## Verificação

Os testes `PlacementTemplates*` em
`qa/tests/eeschema/test_multi_top_level_sheets.cpp` conferem conteúdo, identidade,
propriedade dos membros, pinos de folhas e células de tabelas. As suítes
`MultiTopLevelSheets` e `JunctionPlacement` totalizam 22 casos e 130 verificações.
O fluxo de cliques, diálogo, cancelamento, conexão em fio, listas de rótulos e
unidades U1A/U1B também foi exercitado no editor em uma tela X virtual isolada.
A versão instalada foi conferida com imagens, formas e desfazer; o arquivo
salvo não apresentou identificadores duplicados.
