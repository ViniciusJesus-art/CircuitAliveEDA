# Mouse e colocação de símbolos

No editor de esquemáticos do CircuitAliveEDA:

- **Duplo clique direito** sobre um item usa a mesma ação de **Delete**, com
  histórico de desfazer e atualização das conexões. O alvo é o item sob o cursor;
  uma seleção anterior em outro lugar não é excluída. Campos de símbolo são
  ocultados, como no Delete nativo. Um duplo clique no espaço vazio não exclui itens.
- **Clique direito simples** continua abrindo o menu de contexto. No modo de
  seleção, o menu espera o intervalo de duplo clique configurado pelo sistema
  para distinguir os dois gestos. Durante desenho e outras ferramentas, o menu
  mantém o comportamento nativo.
- **Place repeated copies** vem marcado em **Choose Symbol** e **Choose Power
  Symbol**. Depois de escolher, cada clique esquerdo coloca outra cópia, usando
  a anotação normal do editor e preservando a configuração do símbolo.
- **Esc** descarta apenas a próxima cópia ainda presa ao cursor e encerra a
  colocação. Escolher outra ferramenta também descarta essa prévia e troca de
  função. As cópias já colocadas permanecem no esquema.

A opção **Place all units** mantém seu comportamento: símbolos com várias
unidades são colocados em sequência. Para colocar uma única cópia, desmarque
**Place repeated copies** no seletor.

O reconhecimento do clique direito está em
`eeschema/circuitalive/right_click_gesture.h`; a exclusão usa `ACTIONS::doDelete`
do KiCad. A repetição usa a opção e o fluxo de colocação já existentes, com
pequenos ajustes no seletor e na saída da ferramenta.
