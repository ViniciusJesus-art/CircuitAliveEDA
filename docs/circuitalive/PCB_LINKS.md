# Vínculos entre esquemático e PCB

As páginas principais e hierárquicas de um projeto fazem parte de um único
circuito e alimentam o arquivo `.kicad_pcb` com o nome do projeto. Salve o projeto
para persistir a lista de páginas. Abrir uma página declarada no projeto mantém
essa associação, mesmo que exista um arquivo de projeto antigo com o nome da
página. O editor de PCB também pode abrir as páginas declaradas quando não existe
um esquemático com o nome do projeto.

Use **Tools > Update PCB from Schematic** para revisar e aplicar as mudanças.
No modo normal, por identidade, o vínculo usa:

1. O caminho da instância e o UUID do símbolo, preservando renomeações de referência.
2. Um UUID de símbolo único, para recuperar movimentos entre páginas.

O vínculo é resolvido para a placa inteira antes de alterar referências. Assim,
trocar U1/U2 por U12/U13 ou permutar U1 e U2 atualiza os footprints correspondentes.
Símbolos com várias unidades continuam sendo um único encapsulamento, inclusive
quando suas unidades estão em páginas diferentes. Instâncias repetidas de uma
folha compartilhada mantêm seus vínculos individuais.

Editar valor, referência, campos ou conexões não cria outro footprint. A posição,
a orientação e as trilhas existentes continuam na placa. Alterar o footprint
atribuído segue a opção de substituição da janela de atualização. Componentes
novos recebem footprints novos, desde que tenham uma atribuição válida.

A opção já existente **Re-link footprints ... based on their reference designators**
mantém a associação exclusivamente por referência. Use-a para recuperar associações
antigas ou vincular símbolos recriados aos footprints com a mesma referência.
Para renomear referências preservando a identidade, mantenha essa opção desmarcada.
Referências duplicadas e múltiplos
footprints indistinguíveis impedem a atualização e aparecem no relatório. Variantes
explicitamente configuradas podem ter footprints diferentes com a mesma referência.

Se um símbolo desaparecer do esquema ou for excluído da placa, a próxima atualização
mantém seu footprint, acrescenta o campo visível `CA.LinkStatus` com o texto
**Missing schematic symbol** na camada `Cmts.User` e limpa as redes, funções e tipos
dos pads. O relatório também avisa sobre o componente. O caminho histórico do símbolo
é mantido para permitir recuperar a mesma posição após desfazer a exclusão e atualizar
novamente. O aviso desaparece quando o vínculo é recuperado.

As trilhas de cobre existentes permanecem: limpar a atribuição lógica de um pad
não remove uma ligação física de cobre. Revise o roteamento e execute o DRC após
alterar conexões ou remover componentes. A opção **Delete footprints with no symbols**
remove os footprints sem símbolo; os bloqueios continuam sendo respeitados.
Footprints com o atributo **Board only** conservam seus campos e suas conexões.

Criar uma página encerra ferramentas e prévias na folha original antes da troca.
Os comandos do menu **Pages** aguardam o fechamento do menu antes de abrir diálogos
ou trocar a folha, e a nova página devolve o foco à ferramenta de seleção.
