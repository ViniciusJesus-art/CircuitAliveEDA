# Páginas do esquemático

O menu **Pages**, à esquerda de **Help**, reúne **Previous Sheet**, **Next Sheet**,
**New Page...**, **Edit Page...**, **Delete Page...** e a lista de folhas do projeto. A folha atual
fica marcada. Os comandos de navegação também continuam disponíveis em **View**.

**New Page...** cria uma folha no fim da lista e a abre. Nomes de arquivo já
existentes recebem um sufixo, sem sobrescrever outras folhas. **Edit Page...**
permite alterar o nome e a posição da folha atual. A ordem é armazenada nos
números das folhas e mantida ao salvar e reabrir o projeto.

**Delete Page...** pede confirmação e remove a folha atual e sua hierarquia do
projeto. Não é permitido remover a última folha principal. Os arquivos são
preservados em disco; após salvar, as folhas removidas ficam excluídas da
descoberta automática. Abrir diretamente um arquivo removido o inclui novamente.

Ao abrir um `.kicad_sch`, o editor identifica seu `.kicad_pro` e carrega as folhas
declaradas no projeto, além dos outros `.kicad_sch` da mesma pasta. A folha
selecionada é exibida. Subpastas não são pesquisadas; arquivos de autosave não
são adicionados. Folhas que já fazem parte de uma hierarquia mantêm essa
hierarquia, sem aparecer uma segunda vez como página principal.

Arquivos de outros formatos são ignorados ao descobrir as páginas. A atribuição
de números únicos às folhas encontradas não exibe aviso de arquivo danificado;
o aviso continua disponível quando o leitor realmente repara o conteúdo.

Indicadores digitais, LEDs, botões, ajustes e nomes de sondas da simulação são
associados ao caminho completo da folha. A simulação continua ao navegar, e
cada folha exibe e aceita cliques apenas nos seus próprios controles.

Os menus superiores preservam o foco enquanto estão abertos: um clique abre
o menu e outro escolhe a opção, sem precisar manter o botão do mouse pressionado.
