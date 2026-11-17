# Medição da interface Windows — 17/11/2026

## Ambiente e escopo

Os testes abriram o CircuitAliveEDA instalado em Windows, versão
`10.0.7-alpha.4.1`, commit `631c8548e883b1c6021ef81e39ff24f6b671e1c7`,
correspondente ao checkout antes das alterações locais. O runtime informa
wxPython 4.3.1 / wxWidgets 3.3.3 MSW. Foi usado o demo `ecc83`, copiado para
`.circuitalive/perf/ecc83`, com uma cópia isolada das preferências por
`KICAD_CONFIG_HOME`. Projetos e preferências originais não foram editados.

O problema investigado é abertura de janelas, acionamento de funções e busca
de componentes. Não há medição Fedora nesta sessão.

## Reprodução no aplicativo instalado

A busca mostrou **22.923 símbolos carregados**. A thread da interface foi
consultada por `SendMessageTimeout(WM_NULL)` com limite de 100 ms, em intervalos
nominais de 30 ms. CPU, memória e contadores de I/O foram registrados junto.
As pesquisas válidas conferiram o texto final aceito pelo controle.

| Ação | Pausa observada, aproximadamente |
| --- | --- |
| Primeira abertura da busca de componentes | 4,8 s |
| Segunda e terceira aberturas no mesmo processo | 3,7–3,9 s |
| Pesquisa `resistor` | 2,6 s de respostas bloqueadas; última amostra bloqueada em 3,28 s desde o início da ação |
| Pesquisa `74HC00` | 2,0 s de respostas bloqueadas; última amostra bloqueada em 2,61 s desde o início da ação |
| Abrir Preferências | 1,4 s |
| Abrir Configurações da Página | 0,4 s |
| Abrir Localizar, Ctrl+F | Uma resposta acima de 100 ms; não houve bloqueio prolongado como na busca de componentes |

São medidas de responsividade, com resolução limitada pela sondagem e pelo envio
de teclas; não são tempos exatos de apresentação de cada quadro. Os perfis de
pilha foram coletados em execuções separadas, sem usar seus tempos como benchmark.

Nas duas reaberturas da busca e na consulta `74HC00`, o contador de bytes lidos
do processo não aumentou durante a medição. O atraso se repete com os dados já
carregados: uma explicação baseada apenas no primeiro acesso aos arquivos não
cobre esse comportamento.

Os registros válidos ficaram na pasta local ignorada `.circuitalive/perf/`:
`open-symbol-first.json`, `open-symbol-second.json`, `open-symbol-third.json`,
`typed-resistor.json`, `typed-74hc00.json`, `open-preferences.json`,
`open-page-settings.json` e `open-find.json`.
Tentativas preliminares sem confirmação do texto digitado foram descartadas
da análise da pesquisa.

## Gargalo identificado no filtro de componentes

Em `eeschema/widgets/panel_symbol_chooser.cpp`, `m_choiceFilter` era executado
para cada símbolo/unidade e consultava repetidamente:

- `m_quickList->GetSelection()`, duas vezes no modo All components;
- `m_pinFilter->GetValue()`, uma ou duas vezes;
- as preferências e a identificação textual do símbolo, mesmo sem filtrar
  favoritos ou recentes.

As amostras da thread da interface durante abertura e pesquisa mostraram
leituras em `wxSpinCtrl::GetValue`, controles `wxChoice` e chamadas nativas
`SendMessageW`. Na amostragem de `typed-resistor-profile.stacks`, 103 de 303
amostras continham `NtUserMessageCall`; 39 continham `wxSpinCtrl::GetValue`.
Isso inclui amostras de espera/ociosidade e não equivale a porcentagem de CPU.
Sem PDBs, os nomes internos do KiCad não foram resolvidos; os nomes das funções
wx/Win32 vêm dos símbolos exportados.

Em Windows, esses getters acessam controles nativos por mensagens síncronas.
Repeti-los para dezenas de milhares de nós introduz trabalho da interface dentro
do loop de filtragem. Isso oferece uma explicação concreta para parte da
diferença entre os backends MSW e GTK, embora o ganho relativo no Fedora ainda
precise ser medido.

## Ajuste implementado no fonte

`prepareSearch()` agora captura a seleção da lista e o número de pinos uma vez
por atualização. O predicado usa esses inteiros durante a pontuação dos nós.
As leituras são refeitas em cada nova atualização, inclusive ao alterar os
filtros. No modo All components, também deixa de formatar identificações e
consultar favoritos/recentes sem necessidade.

A alteração usa o mesmo caminho nas duas plataformas e mantém as regras de
busca, pinos, favoritos, recentes e encapsulamento. Não há bifurcação do projeto
nem alteração dos modelos de simulação.

## Benchmark isolado dos controles

O script versionado `tools/circuitalive/benchmark-chooser-controls.py` cria
controles wx ocultos e compara os getters repetidos com valores capturados.
Foi executado com o Python empacotado no próprio CircuitAliveEDA. Verifica que
os dois caminhos usam os mesmos valores e produz JSON.

Medianas de três execuções para 22.923 iterações:

| Modo / pinos | Leituras repetidas | Leitura única | Razão |
| --- | --- | --- | --- |
| All / 0 | 181,4 ms | 2,7 ms | 66,9× |
| Favorites / 14 | 333,2 ms | 4,4 ms | 76,3× |
| Recent / 2 | 313,6 ms | 6,1 ms | 51,7× |

Esse benchmark usa chamadas Python ao wx e isola somente o custo dos getters;
não representa uma aceleração de 52–76× da janela ou do aplicativo completo.
Resultado local: `.circuitalive/perf/control-benchmark-final.json`.

Para repetir no Windows, em PowerShell, usando a instalação do aplicativo:

```powershell
$appRoot = Join-Path $env:LOCALAPPDATA 'Programs/CircuitAliveEDA'
$env:PYTHONHOME = Join-Path $appRoot 'bin'
& (Join-Path $appRoot 'bin/python.exe') tools/circuitalive/benchmark-chooser-controls.py
Remove-Item Env:PYTHONHOME
```

Em Linux, execute com o Python que tenha o wxPython do ambiente de build e uma
sessão gráfica disponível.

## Validação do editor recompilado

As 133 dependências vcpkg foram preparadas, e o build MSVC Release dos alvos
`eeschema` e `qa_common` terminou com sucesso: 1.360 etapas. O teste usou uma
cópia física do runtime em `.circuitalive/perf-runtime`, com o editor, módulo
`_eeschema.dll`, `kicommon.dll`, `kigal.dll`, `kiapi.dll` e worker recompilados,
além das DLLs da instalação vcpkg local. Os hashes dos seis binários foram
conferidos contra os arquivos do build e registrados em `EDITOR-PERF-BUILD.json`.
O aplicativo instalado permaneceu intacto. Os demais módulos do runtime de teste
vieram da instalação existente; não é um instalador completo novo.

Foi usado o mesmo demo, configuração isolada e conjunto de 22.923 símbolos.
O build já estava encerrado durante as medições. A metodologia de sondagem foi
a mesma da reprodução anterior.

| Ação | Executável anterior | Editor recompilado |
| --- | --- | --- |
| Primeira abertura do seletor | Pausa até 4,80 s | Pausa até 0,81 s |
| Segunda abertura, busca vazia | Pausa até 3,72 s | Pausa até 0,26 s |
| Terceira abertura, busca vazia | Última amostra bloqueada em 3,91 s | Última amostra bloqueada em 0,26 s |
| `resistor`, bibliotecas aquecidas | 19 amostras bloqueadas; última em 3,28 s | Nenhuma resposta acima de 100 ms |
| `74HC00`, bibliotecas aquecidas | 15 amostras bloqueadas; última em 2,61 s | Nenhuma resposta acima de 100 ms |

São pausas de responsividade da thread da interface, não tempos exatos de
apresentação da janela ou de conclusão da busca. Ausência de respostas acima
de 100 ms não significa que a operação inteira terminou em menos de 100 ms.
Os textos finais `resistor` e `74hc00` foram confirmados, e a inspeção visual
mostrou o `74HC00` correspondente na árvore e na prévia.

O consumo de CPU durante a primeira abertura caiu de aproximadamente 5,80 s
para 0,67 s no intervalo observado. Nas pesquisas válidas do editor novo e nas
reaberturas, o contador de bytes lidos não aumentou.

Uma tentativa inicial de pesquisa coincidiu com a carga de bibliotecas de
visualização, com aumento de cerca de 166 MB nos bytes lidos pelo processo.
A automação não conseguiu confirmar o controle ao final dessa tentativa;
ela foi descartada. A tabela compara consultas com bibliotecas aquecidas.
O custo dessa primeira carga ainda precisa de uma medição própria.

Também foram abertas Preferências, Configurações da Página e Localizar.
As pausas observadas no editor recompilado foram aproximadamente 0,39 s,
0,11 s e nenhuma resposta acima de 100 ms, respectivamente. Não apareceu
regressão nessa sessão. Como essas janelas não usam o filtro alterado, essa
comparação entre builds não isola a causa das diferenças nelas.

Registros locais: `improved-open-first.json`, `improved-open-second.json`,
`improved-open-third.json`, `improved-typed-resistor.json`,
`improved-typed-74hc00.json`, `improved-open-preferences.json`,
`improved-open-page-settings.json` e `improved-open-find.json`, todos em
`.circuitalive/perf/`. `summarize-ui.py` consolida as sondagens.

## Conferência funcional

As suítes C++ existentes `ComponentChooserSearch` e `LibTreeModelAdapter`
passaram no `qa_common` recompilado: **9 testes selecionados e 64 verificações**.
Cobrem interpretação de consultas físicas/textuais, pinos, dimensões, termos
equivalentes, preferências de footprint, atualização de metadados e classificação
dos resultados. Os demais testes de `qa_common` não foram executados.

Na interface recompilada, o roteiro verificou:

- adicionar `74xx:74HC00` aos favoritos, filtrar e remover o favorito temporário;
- mostrar o componente com 14 pinos, omiti-lo com 13 e restaurá-lo com 14;
- limpar a busca e os filtros físicos com Reset filters;
- ativar e desativar Advanced (KiCad), conferindo o estado salvo pelo aplicativo;
- alternar Recent e All components e confirmar o retorno dos resultados.

Os testes usaram somente as preferências copiadas. O favorito temporário foi
removido, e o modo avançado voltou a desativado. Registro local:
`.circuitalive/perf/improved-functional-filters.json`.

A preparação revelou um problema separado de build em Windows localizado:
o prefixo de `/showIncludes` em português foi detectado com codificação incorreta.
Foram instalados os recursos English do Visual Studio, e o builder e o preset
Windows passaram a usar `VSLANG=1033`. A nova detecção produz o prefixo ASCII
esperado pelo Ninja. As nove fases do teste de inicialização do toolchain passam.

O custo de criar outras janelas não pode ser atribuído integralmente ao filtro
da busca. Preferências e Configurações da Página tiveram pausas menores e
precisam de perfis próprios caso continuem problemáticas. Há ainda um caminho
separado em `pcbnew/pcbnew.cpp::filterFootprints()` que aguarda o carregamento
das bibliotecas de footprints na thread chamadora (`BlockUntilLoaded()`); a
primeira carga deve ser medida separadamente das consultas já aquecidas.

## Limites restantes

Não há ambiente Linux/Fedora instalado nesta máquina. A mudança mantém as regras
compartilhadas e não altera o preset Fedora, mas ainda requer compilação e teste
da interface no Fedora antes de distribuir uma release. Esta sessão validou o
editor Windows recompilado e os testes selecionados; não gerou nem publicou um
instalador ou uma release.

Para reabrir a cópia de teste neste checkout, execute em PowerShell:

```powershell
./.circuitalive/perf/launch-improved.ps1
```
