# Validação da etapa 0

Executada em 16/10/2026, no Fedora Linux 44 x86_64 desta máquina.

## Base entregue

- Fork remoto: <https://github.com/ViniciusJesus-art/CircuitAliveEDA>.
- Branch padrão e de trabalho: `circuitalive/main`.
- Fontes KiCad na raiz, base `6809ada8746084720c0093eb484c889afb35d0d4`.
- Remotes `origin`, `upstream` e `upstream-gitlab` configurados.
- Código C++ original preservado; os acréscimos desta etapa são configuração,
  scripts, lançador e documentação.

## Ambiente verificado

| Componente | Versão |
|---|---|
| GCC / padrão | 16.2.1 / C++20 |
| CMake / Ninja | 4.3.0 / 1.13.2 |
| wxWidgets / wxPython | 3.2.9 / 4.2.4 |
| Boost | 1.90.0 |
| OpenCascade | 7.9.3 |
| Python / SWIG | 3.14.7 / 4.4.1 |
| Protobuf | 3.19.6 |
| libngspice / code models | 47 / 47 |

O binário informa `10.0.7-unknown-CircuitAliveEDA`. O `unknown` vem da versão
de desenvolvimento declarada pelo upstream, em um checkout raso sem tags.

Os módulos `analog.cm`, `digital.cm`, `spice2poly.cm`, `table.cm`, `tlines.cm`,
`xtradev.cm` e `xtraevt.cm` estão presentes. A biblioteca compartilhada exporta
`ngSpice_Init`, `ngSpice_Command`, `ngSpice_Init_Evt` e `ngCM_Input_Path`.
Isso confirma a instalação da API e dos code models; a continuidade e o fluxo
de eventos para a futura sessão interativa ainda serão objeto da etapa 1.

## Resultados

| Verificação executada | Resultado |
|---|---|
| Build completo padrão, incluindo editores, CLI, 3D e alvos QA | Aprovado |
| Instalação em `.circuitalive/install`, sem instalação global | Aprovado |
| Build incremental sem alterações | Sem recompilar objetos ou refazer links; apenas alvos administrativos originais |
| Bloqueio de uma segunda compilação simultânea | Aprovado |
| `./circuitalive cli version --format about` | Versão e recursos identificados |
| Exportação de netlist SPICE pela CLI instalada | Aprovado, incluindo caminho de modelo com espaços |
| Lançador padrão abrindo `rectifier.kicad_pro` no gerenciador | Aprovado |
| Eeschema instalado abrindo `rectifier.kicad_sch` | Aprovado |
| Análise `.tran 1u 10m` pela interface do simulador | 10.008 pontos; curvas de entrada e saída exibidas |
| Suíte original `qa_spice` | 53 casos aprovados; 2,05 s; zero falhas |
| Tabelas globais | 223 entradas de símbolos e 155 de footprints, zero caminhos ausentes |
| Reabertura e encerramento normal do Eeschema | Aprovado |

A validação gráfica usou Xvfb com renderização por software e uma cópia do
exemplo original em `.circuitalive/smoke/rectifier`. A configuração inicial
foi concluída no perfil local do projeto. A configuração do KiCad instalado
no Fedora não foi usada como perfil de execução do fork.

Durante a preparação, um cabeçalho pré-compilado foi regenerado após uma
sobreposição de processos de build. O build completo subsequente e os testes
passaram. O script agora bloqueia compilações simultâneas. O preset também
desativa o rastreamento adicional de dependências pelo linker GNU, que não
escapou espaços em caminhos de bibliotecas importadas. As dependências
explícitas do CMake permanecem ativas.

O GCC emitiu avisos em código original do upstream. Não foi realizada uma
validação exaustiva de todos os importadores, recursos 3D ou demais suítes QA;
os executáveis QA foram compilados, e a suíte executada foi `qa_spice`.

## Evidências locais

Arquivos gerados, ignorados pelo Git, em `.circuitalive/logs/`:

- `build-complete.log`: conclusão do build completo.
- `build-link-fix.log`: links finais com o ajuste de dependências do linker.
- `build-incremental-check.log`: retomada incremental verificada.
- `install.log`: instalação local.
- `version-about.txt`, `version-commit.txt`, `environment.txt`.
- `ngspice-environment.txt`: API, módulos e checksums XSPICE.
- `test-spice.log`: saída do CTest e dos 53 casos SPICE.
- `rectifier.cir`: netlist exportada pela CLI instalada.
- `eeschema-rectifier.png`, `rectifier-simulation.png`, `kicad-manager.png`.

Os comandos para repetir a preparação e a execução estão em
[BUILD_FEDORA.md](BUILD_FEDORA.md). Esta entrega é a base para implementar
a simulação interativa; os novos controles Play/Pause/Stop ainda não foram
adicionados ao editor.
