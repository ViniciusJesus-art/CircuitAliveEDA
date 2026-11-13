# Escolha de componentes e edição durante a simulação

A janela **Choose Symbol** reúne a lista, o símbolo, o footprint em 2D e o
modelo 3D do footprint. A descrição inferior mostra somente o nome e a
descrição do símbolo, deixando mais espaço para a lista.

## Busca e filtros

| Entrada | Comportamento |
|---|---|
| `resistor 1/4W` | Busca resistores e footprints compatíveis de 0,25 W |
| `DIP 14` | Filtra símbolos de 14 pinos que admitem DIP e seus footprints DIP |
| `LED 5mm` | Busca LEDs e footprints com diâmetro de 5 mm nos metadados |
| `74HC00` | Busca pelo código do componente |
| `74HC00 SMD SOIC-14` | Combina código, montagem, encapsulamento e pinos |
| `10,5x8mm` | Limita as dimensões do footprint a 10,5 × 8 mm |

Os controles físicos permitem escolher PTH/SMD, número de pinos,
encapsulamento/tamanho/potência e largura/altura máximas. Zero nas dimensões
significa sem limite; a medição usa a extensão real do footprint sem os textos.
Zero em pinos usa a contagem do símbolo ou a contagem explicitada na busca.
Símbolos com várias unidades usam a contagem física completa; símbolos
herdados também incluem os pinos fornecidos pelo símbolo base.

Os footprints continuam respeitando os filtros da biblioteca e os números
de pads. Na busca física, nomes antigos como `SO14*` também encontram os
equivalentes atuais `SOIC-14_*`. Essa equivalência exige o separador do
encapsulamento, evitando confundir SOIC-14 com SOIC-14-16, que tem pinos
removidos e numeração diferente. A opção SMD prioriza encapsulamentos do
componente antes de soquetes SMD, mantendo ambos disponíveis na lista.
Quando nenhum footprint atende aos filtros, a janela informa isso e limpa
a prévia. **Reset filters** limpa a busca e os controles físicos.

**Advanced (KiCad)** mantém a sintaxe original de busca e o acesso às
bibliotecas originais. A caixa de busca inteligente não limita as bibliotecas
habilitadas pelo usuário.

## Preferências globais

O botão de estrela adiciona ou remove o símbolo dos favoritos. O seletor
superior dá acesso a todos os componentes, favoritos ou recentes. Os recentes
guardam os últimos 30 símbolos aceitos na janela, inclusive entre projetos.

O primeiro item da lista de footprints é sempre **empty**, inclusive nas buscas
físicas e no modo avançado. Escolhê-lo remove o footprint desta colocação;
selecionar outro item também altera somente o componente que será colocado.
Ajustar os filtros preserva uma escolha explícita de `empty` para o símbolo
atual. Uma nova escolha carrega novamente o padrão salvo.

O botão **Make default for this symbol** salva a escolha atual para as próximas
colocações em todos os projetos. Também aceita `empty`: nesse caso, o símbolo
continua sem footprint nas próximas escolhas, mesmo que a biblioteca tenha uma
associação. Escolher um item sem clicar no botão não altera uma preferência
já salva.

Para símbolos sem footprint na biblioteca e sem preferência pessoal, a primeira
colocação com um footprint atribuído cria automaticamente a preferência global.
Essa inicialização não substitui preferências existentes nem grava `empty`
automaticamente; um padrão vazio exige o botão, como qualquer escolha explícita.

O footprint escolhido acompanha o símbolo colocado no esquema. As
preferências ficam em `eeschema.json`, no grupo `symbol_chooser`, no diretório
global de configuração da aplicação. Os arquivos das bibliotecas não são
alterados. Uma preferência vazia junto da autorização explícita de substituição
registra o padrão `empty`. Preferências vazias antigas sem essa autorização
continuam usando a associação da biblioteca.

## Prévia 3D

O painel usa o canvas, o carregador e o cache 3D nativos do PCB Editor, com
os modelos reais associados ao footprint. É possível arrastar para girar e
usar a roda para aproximar. O enquadramento considera a geometria dos modelos,
incluindo escalas, rotações e deslocamentos, depois da carga do canvas.
A perspectiva inicial mantém o eixo Z (azul) apontando para cima.
Footprints sem modelo mostram essa indicação. O painel usa configurações
locais e não modifica as preferências do visualizador 3D da placa.

## Simulação interativa

Enquanto o estado da simulação é `RUNNING`, o duplo clique esquerdo no esquema
não dispara as janelas de propriedades/configuração. Os cliques simples dos
controles interativos continuam disponíveis. Em Pause e Stop, o duplo clique
retoma o comportamento normal.

## Validação

Os testes `ComponentChooserSearch` e `LibTreeModelAdapter` cobrem a separação
de termos físicos, pinos, dimensões decimais, potência equivalente, nomes de
encapsulamentos, padrão vazio explícito e atribuição automática inicial.
São 8 casos e 59 verificações aprovadas.

Os testes de interface em Xvfb usam cópias de projetos e configuração própria:
buscas dos exemplos, modelos 3D reais com Z para cima, favoritos/recentes,
filtros físicos e fechamento da janela. Componentes realmente colocados e
salvos confirmam que escolhas locais não alteram padrões existentes. Outro
projeto confirma `empty` global, inclusive para símbolos com footprint na
biblioteca, e a atribuição automática inicial de um LED sem preferência.
A simulação foi executada de fato: cliques rápidos no resistor,
campo de valor, LED excluído e fonte não abriram propriedades em execução;
em Pause e Stop elas voltaram a abrir normalmente.

As evidências locais ficam em
`.circuitalive/experiments/component-chooser/` e
`.circuitalive/experiments/chooser-empty/`.
