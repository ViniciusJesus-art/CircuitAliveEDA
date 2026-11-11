# Aplicar propriedades por clique

A ferramenta **Apply Properties by Click...** está na barra superior, depois de
Localizar/Substituir, e no menu **Tools**. O atalho padrão é **Ctrl+Shift+P** e
pode ser alterado nas preferências de atalhos.

1. Escolha a propriedade e informe o valor ou padrão.
2. Opcionalmente, habilite a numeração e configure início e passo.
3. Confirme em **OK** e clique nos objetos desejados.
4. Use **Esc** para sair. O cursor identifica a ferramenta e mostra o próximo valor.

Exemplo: `Digital_D#`, início `10`, passo `2`, aplica `Digital_D10`,
`Digital_D12`, `Digital_D14` e assim por diante. `Digital_D?` produz a mesma
sequência. Todas as ocorrências de `#` e `?` recebem o mesmo número do clique.
Passos negativos decrementam; passo zero mantém o número. Com a numeração
desativada, esses caracteres são literais. Cliques vazios, cancelados ou recusados
não consomem números; a sequência encerra ao atingir o limite numérico.

## Propriedades

Há opções para referência, valor, footprint, datasheet, descrição, nome/texto,
folhas e campos personalizados. A lista também inclui as propriedades editáveis
dos objetos da página, como tamanho de texto, orientação, visibilidade e flags.
Cada propriedade se aplica aos tipos de objeto que a oferecem. Campos gerados
e propriedades somente de leitura são protegidos.

**Name / text** modifica o valor de componentes, o texto de rótulos e textos,
ou o nome de uma folha. Clicar no texto de um campo também permite atingir seu
componente. Campos personalizados ausentes podem ser criados; são inicialmente
ocultos, na posição do objeto, e podem ser exibidos pelo editor de propriedades.

Use valores como `true`/`false` ou `1`/`0` para flags. Propriedades numéricas
aceitam unidades do editor, por exemplo `0.8 mm`. Propriedades com opções usam
os nomes exibidos no painel de propriedades. Valores inválidos e referências
duplicadas são recusados antes da aplicação.

As edições comuns têm desfazer/refazer por clique. A troca de arquivo de uma
folha segue o fluxo nativo de arquivos, com suas confirmações, e pode invalidar
o histórico de desfazer, como no diálogo de propriedades da folha.
Campos de um componente
com várias unidades são sincronizados sem alterar os outros campos. Edições de
campos respeitam a variante ativa. Reabrir a ferramenta conserva sua configuração
durante a sessão e inicia a sequência novamente no início configurado.

## Rótulos em fios e pinos

Com **Create labels on wires or pin endpoints** habilitado, clicar diretamente em um
fio ou na ponta de conexão de um pino cria um rótulo conectado usando o valor
gerado, independentemente do campo selecionado. É possível escolher rótulo local, global ou hierárquico. O ponto
de conexão fica exatamente sobre o segmento ou na ponta elétrica do pino; um
rótulo já ancorado nesse ponto é atualizado em vez de receber uma cópia sobreposta. Rótulos com o mesmo nome seguem as regras
normais de conexão e escopo do esquemático.

Desative essa opção para aplicar propriedades do próprio fio, como largura,
estilo ou cor. Para renomear um rótulo existente, escolha **Name / text** e clique
no seu texto.

## Verificação

A suíte `PropertyStamp`, em `qa/tests/eeschema/test_property_stamp.cpp`, cobre
sequências, limites numéricos, validação, campos personalizados, referências,
propriedades nativas, unidades de componentes e variantes. O fluxo de cliques,
atalho, conversão em milímetros, desfazer/refazer e criação de rótulos foi
exercitado no editor em uma tela virtual. A conexão do rótulo ao fio foi
conferida na netlist exportada.

O seletor de propriedades usa um clique para abrir e outro para escolher, com
uma lista de altura limitada e rolagem. Em pinos, a orientação da label segue
a direção do terminal para fora do corpo do componente, inclusive após rotação
ou espelhamento; isso vale para labels locais, globais e hierárquicas.
