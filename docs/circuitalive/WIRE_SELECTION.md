# Seleção e arraste de fios e trilhas

No esquema, um clique simples em um fio seleciona as retas conectadas até o
próximo terminal, entrada de barramento, junção ou derivação. Delete remove o
trecho selecionado com uma única operação de undo. O percurso segue a geometria
da folha; labels com o mesmo nome em outro local não ampliam a seleção.
Cruzamentos sem conexão permanecem separados.

Pressionar e arrastar uma reta ajusta essa reta, inclusive quando o clique
anterior destacou o trecho inteiro. As ligações adjacentes se ajustam para
manter a conexão. Esse gesto também funciona sobre um fio ainda não selecionado
com a preferência padrão de arrastar itens selecionados. A preferência explícita
de usar o arraste apenas para seleção continua sendo respeitada.

Uma seleção feita pelo retângulo ou laço conserva todos os itens selecionados
durante o arraste. Os fios destacados apenas por um clique são identificados
separadamente dessa seleção de área. O movimento começa pelo ponto pressionado,
sem saltar para uma quina do trecho. Os atalhos M/G conservam seu funcionamento.

No PCB Editor, o clique simples em uma trilha ou arco usa a primeira etapa da
seleção nativa por U: seleciona o trecho até uma derivação, pad ou via. U continua
ampliando a seleção para as etapas seguintes. Pressionar e arrastar uma reta desse
trecho encaminha somente a reta pressionada ao roteador: ela desliza e os cantos
adjacentes aumentam ou diminuem, conservando as conexões aos pads. Isso funciona
em retas horizontais, verticais e diagonais, inclusive quando o clique anterior
foi feito em outra reta do mesmo trecho. As preferências de arraste de trilhas
continuam sendo respeitadas; a padrão usa o roteador de 45°.

Selecionar por retângulo ou laço permite mover todos os itens selecionados como
um conjunto, conservando sua geometria. Essa seleção não é reduzida à reta
pressionada nem passada ao arraste individual do roteador. Os modificadores e
as etapas explícitas de U conservam seu funcionamento. Esse padrão é distribuído
no código do editor, sem depender de preferências gravadas no perfil de
desenvolvimento.

Quando existe uma seleção no esquema, o primeiro clique em área vazia somente
desmarca os itens. O clique seguinte pode iniciar a prévia da última ferramenta
ou do componente escolhido em Used Components, seguindo o fluxo existente.

## Validação

- Compilação de eeschema, pcbnew e das suítes de testes correspondentes.
- 24 testes do esquemático: percurso geométrico, grupos e ferramentas de pinos.
- 8 testes de commits e grupos e 2 testes de arraste de arcos no PCB.
- Gestos reais em um display virtual isolado: clique/Delete/Undo nos dois
  editores, expansão por U, arraste de reta com e sem seleção anterior, arraste
  de seleção retangular e primeiro/segundo clique vazio com um resistor escolhido.
- PCB com dois pads fixos e cinco trilhas ligadas por cantos de 45°: arrastes
  horizontais para cima/baixo, vertical e diagonal, conferindo continuidade,
  posição dos pads, ajuste dos cantos e restauração por um único undo.
- Seleção retangular das trilhas no PCB: movimento conjunto conferido por uma
  translação comum a todas as retas, sem alterar seus comprimentos ou ângulos.
- As coordenadas e os totais de fios/trilhas foram conferidos nos arquivos salvos.

Os testes de percurso ficam em
`qa/tests/eeschema/test_wire_segment_selection.cpp`.
