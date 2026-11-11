# Vínculos entre esquemático e PCB

As páginas principais e hierárquicas de um projeto fazem parte de um único
circuito e alimentam o arquivo `.kicad_pcb` com o nome do projeto. Salve o projeto
para persistir a lista de páginas. Abrir uma página declarada no projeto mantém
essa associação, mesmo que exista um arquivo de projeto antigo com o nome da
página. O editor de PCB também pode abrir as páginas declaradas quando não existe
um esquemático com o nome do projeto.

## Vínculo por Reference

A opção **Re-link footprints ... based on their reference designators** fica
marcada por padrão na janela **Tools > Update PCB from Schematic**. A associação
é resolvida para a placa inteira antes de aplicar mudanças:

1. A **Reference** tem prioridade: o U1 do esquema usa o U1 já existente na PCB.
2. Se a nova referência ainda não tiver um footprint associado, o caminho da
   instância e o UUID do símbolo recuperam a posição anterior após uma renomeação.
3. Um UUID de símbolo único também permite recuperar movimentos entre páginas.

Assim, renomear U1/U2 para U12/U13 atualiza as peças existentes quando U12/U13
não estão na placa. Ao permutar referências que já existem na PCB, prevalece o
vínculo por Reference. Desmarque a opção de revincular por referência para usar
somente a identidade do símbolo, inclusive ao permutar referências.

Símbolos com várias unidades continuam sendo um único encapsulamento, inclusive
quando suas unidades estão em páginas diferentes. Instâncias repetidas de uma
folha compartilhada mantêm seus vínculos individuais. Referências duplicadas e
múltiplos footprints indistinguíveis impedem a atualização e aparecem no relatório.
Variantes explicitamente configuradas podem ter footprints diferentes com a mesma
referência.

## Atualização ao retornar à PCB

Abra a PCB pelo projeto para usar a comunicação entre os editores. Trazer o
editor de PCB para o foco sincroniza automaticamente os componentes já
posicionados: conexões, referências, valores, campos e footprints atribuídos.
São consideradas todas as páginas e as edições atuais do esquemático, mesmo
antes de salvar. A sincronização aguarda ferramentas e diálogos ativos terminarem;
símbolos novos precisam estar anotados com referências válidas.

Essa atualização mantém a posição e a orientação das peças, usa a substituição
normal quando a atribuição de footprint muda e não insere componentes novos.
As mudanças entram no histórico de desfazer da PCB. A placa continua precisando
ser salva normalmente. A janela **Update PCB from Schematic** permanece disponível
para revisar o relatório, escolher opções e adicionar componentes em lote.

Se um símbolo desaparecer do esquema ou for excluído da placa, a próxima atualização
mantém seu footprint, acrescenta o campo visível `CA.LinkStatus` com o texto
**Missing schematic symbol** na camada `Cmts.User` e limpa as redes, funções e tipos
dos pads. O relatório também avisa sobre o componente. O caminho histórico do símbolo
é mantido para permitir recuperar a mesma posição após desfazer a exclusão e atualizar
novamente. O aviso desaparece quando o vínculo é recuperado.

No canvas da PCB, o footprint e seus pads ficam escurecidos, sem selo adicional,
mesmo sem seleção e com a camada de comentários oculta.
Ao sincronizar, os trechos fisicamente conectados de trilhas, arcos e vias são
comparados com os pinos do esquema. Se um símbolo/pino desaparecer ou o trecho
passar a unir redes diferentes, o cobre recebe o mesmo escurecimento e trilhas
e arcos exibem **MISS** na própria linha do cobre, com a orientação e o tamanho
do texto de rede, sem caixa ou etiqueta flutuante. Apenas renomear uma rede, mantendo a ligação entre os
mesmos pinos, não causa esse aviso. Os avisos permanecem ao salvar e reabrir,
participam do desfazer/refazer e são retirados na sincronização após restaurar
as ligações. São indicadores de tela; não acrescentam desenho aos Gerbers nem
às impressões. Um trecho marcado pode conter um curto físico: o cobre continua
na placa e precisa ser revisado.

As trilhas de cobre existentes permanecem: limpar a atribuição lógica de um pad
não remove uma ligação física de cobre. Revise o roteamento e execute o DRC após
alterar conexões ou remover componentes. A opção **Delete footprints with no symbols**
remove os footprints sem símbolo; os bloqueios continuam sendo respeitados.
Footprints com o atributo **Board only** conservam seus campos e suas conexões.

## Painel Pending Footprints

O painel reúne os componentes de todas as páginas que vão para a PCB e ainda
não estão posicionados. Cada linha mostra **Reference** e **Value**, com uma prévia
do footprint selecionado. Componentes excluídos da placa não entram na lista.
Uma atribuição ausente aparece como **(no footprint)**; atribua o footprint no
esquemático antes de tentar posicioná-lo.

1. Selecione uma linha no painel.
2. Clique em uma área vazia da PCB para trazer o footprint ao ponteiro.
3. Mova a prévia e clique novamente para posicionar. Rotação e troca de lado
   também funcionam durante a prévia.
4. O item sai da lista, e o próximo clique vazio traz o componente seguinte,
   sem precisar voltar ao painel.

Clicar sobre um footprint, trilha, texto ou outro objeto existente mantém a
seleção normal do editor. **Esc** cancela a prévia sem inserir o componente;
um segundo **Esc** encerra a escolha. Trocar de janela também cancela a prévia,
permitindo atualizar o esquema antes de continuar. Desfazer um posicionamento
devolve o componente à lista, e refazer o remove novamente.

A lista se atualiza junto com a PCB ao retornar ao editor, incluindo componentes
adicionados ou excluídos no esquema e mudanças em referência, valor e footprint.
Ela também acompanha alterações locais e posicionamentos na placa. O botão
**Float / dock** alterna entre painel acoplado e janela flutuante; a disposição
é salva. **View > Panels > Pending Footprints** mostra ou oculta o painel.

Criar uma página encerra ferramentas e prévias na folha original antes da troca.
Os comandos do menu **Pages** aguardam o fechamento do menu antes de abrir diálogos
ou trocar a folha, e a nova página devolve o foco à ferramenta de seleção.
