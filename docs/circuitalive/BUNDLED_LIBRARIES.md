# Bibliotecas prontas na primeira execução

Todos os formatos distribuídos — EXE, RPM, DEB e AppImage — incluem símbolos,
footprints, modelos 3D, templates de projetos, modelos e blocos de simulação e
seus runtimes. A instalação não depende de um KiCad instalado no computador nem
de downloads de bibliotecas na primeira abertura.

O preparo por código-fonte usa as regras de instalação dos repositórios KiCad
10.0.7. Copiar apenas o checkout não produz o mesmo resultado: os símbolos
`.kicad_symdir` precisam ser empacotados, e `sym-lib-table` e `fp-lib-table`
precisam ir para `share/kicad/template`. Veja as regras oficiais de
[símbolos](https://gitlab.com/kicad/libraries/kicad-symbols/-/blob/10.0.7/CMakeLists.txt)
e [footprints](https://gitlab.com/kicad/libraries/kicad-footprints/-/blob/10.0.7/CMakeLists.txt).
Windows e DEB agora executam configure, build e install dessas bibliotecas;
RPM/AppImage recebem os dados já instalados dos pacotes Fedora.

O código comum inicializa as tabelas globais antes de abrir o gerenciador,
editores independentes ou CLI. As referências usam `${KICAD10_TEMPLATE_DIR}`,
e os caminhos padrão acompanham a instalação. Links gerenciados de instalações
antigas são atualizados sem remover bibliotecas pessoais. Tabelas vazias da
instalação anterior são preenchidas uma vez, com registro em
`circuitalive-library-setup` no perfil. Depois disso, uma tabela esvaziada pelo
usuário permanece vazia. Uma tabela inválida existente é preservada para reparo.

O empacotamento recusa bibliotecas ou tabelas ausentes, entradas que não apontam
para arquivos instalados, símbolos sem registro, modelos 3D ou templates ausentes
e falta de modelos/blocos de simulação. O teste da instalação usa um perfil limpo
e confere se a CLI inicializa as mesmas tabelas usadas pela interface. EXE, DEB
e o bundle Fedora também exportam um símbolo real a partir dos dados incluídos.
AppImage verifica a inicialização pelo seu próprio ponto de entrada.

As alterações de inicialização exigem recompilar os aplicativos. Reempacotar um
runtime antigo não incorpora essa correção; a verificação da primeira execução
recusará um runtime que não registrar as bibliotecas.

Verificações locais:

```bash
python3 qa/tests/cli/test_component_libraries.py
python3 qa/tests/cli/test_windows_installer.py
python3 qa/tests/cli/test_release_artifacts.py
build/circuitalive/qa/tests/common/qa_common --run_test=LibraryTables
```

Os testes locais de scripts não substituem a instalação nativa do EXE no Windows
nem a geração e extração dos pacotes finais nos respectivos runners.
