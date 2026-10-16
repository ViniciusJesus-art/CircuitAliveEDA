# CircuitAliveEDA: base e compilação no Fedora

## Escopo

Esta é a etapa 0 do [mapa](Mapa_KiCad_Simulacao_Interativa.md): base estável, checkout
completo, ambiente de compilação, instalação local e verificação do KiCad original.
O núcleo de sessão Play/Pause/Stop foi acrescentado na
[etapa 1](INTERACTIVE_SESSION.md); a integração visual ao editor é a etapa seguinte.

Base fixada: **KiCad 10.0.7**, tag oficial `10.0.7`, commit
[`69cafb907ebc09e7722a2a464b1d466fc2e3608d`](https://gitlab.com/kicad/code/kicad/-/tags/10.0.7),
publicada em 06/10/2026. O registro legível por ferramentas está em
[BASELINE.json](BASELINE.json). Essa é a base para as próximas etapas.

A revisão `6809ada8746084720c0093eb484c889afb35d0d4` do mapa original foi
substituída: ela continha 30 commits posteriores à versão estável. O mapa é
mantido como documento de referência, sem alterar seu conteúdo original.
Os commits de configuração do projeto foram reaplicados sobre a tag estável;
nenhum código C++ do KiCad foi modificado na preparação da etapa 0.
A etapa 1 adiciona módulos novos de sessão, sem alterar o wrapper tradicional de ngspice.
O Git acrescenta a quantidade e o hash dos commits do projeto à versão de
compilação, além do sufixo `CircuitAliveEDA`; isso não altera a base upstream.

## Organização

```text
CircuitAliveEDA/
├── CMakeLists.txt, eeschema/, pcbnew/, common/, ...  fontes do KiCad
├── CMakePresets.json                              presets do projeto
├── circuitalive                                  lançador
├── tools/circuitalive/                           scripts de desenvolvimento
├── docs/circuitalive/                            mapa e documentação
├── build/circuitalive/                           objetos e executáveis de build
└── .circuitalive/                                arquivos locais, ignorados no Git
    ├── deps/                                    dependências Fedora extraídas
    ├── rpms/                                    cache de download
    ├── install/                                 instalação local completa
    ├── config/, cache/, documents/               dados da execução
    ├── test-config/                              configuração dos testes
    ├── ccache/                                   cache de compilação
    ├── github-auth/                              autenticação local do GitHub CLI
    └── logs/                                    evidências da configuração e build
```

Não há subpasta contendo outro checkout do KiCad. O atalho
`/tmp/circuitalive-UID-HASH` aponta para esta raiz e é recriado automaticamente
pelos scripts. Ele evita problemas de ferramentas de terceiros com os espaços
no caminho. Fontes, objetos e dependências continuam fisicamente no projeto.
Depois de reiniciar a máquina, use os scripts para recriar o atalho antes de
chamar Ninja, CMake ou executáveis diretamente. Ao mover o projeto para outro
caminho, configure uma nova árvore de build.

## Dependências

Plataforma inicial: Fedora Linux 44, x86_64. O código requer C++20 e CMake >= 3.22.
Os presets mantêm esquemáticos, PCB, 3D/OpenCascade, API IPC, Python/wxPython e
testes SPICE habilitados. Telemetria Sentry e busca de atualizações ficam
desabilitadas para o CircuitAliveEDA.

Em uma máquina nova, a opção recomendada é instalar as dependências pelo Fedora:

```bash
tools/circuitalive/dependencies-fedora.sh --system
```

Esse comando usa `sudo dnf install` e solicita a senha pelo próprio `sudo`.
Instale também o pacote `kicad` ou forneça as bibliotecas oficiais separadamente;
os fontes do aplicativo não contêm a coleção completa de símbolos, footprints e
modelos 3D.

Nesta máquina, as bibliotecas de execução e as bibliotecas de componentes já
existiam, mas faltavam os pacotes de desenvolvimento. Como `sudo` solicita senha,
os cabeçalhos e ferramentas ausentes foram extraídos de RPMs oficiais dentro de
`.circuitalive/deps`. Este modo complementa a instalação existente do Fedora 44:

```bash
tools/circuitalive/dependencies-fedora.sh --local
```

Esse modo inclui versões fixadas para combinar com as bibliotecas instaladas
nesta máquina e depende da disponibilidade desses RPMs nos repositórios. Não
equivale a um contêiner nem a um SDK independente do sistema. Em outro Fedora,
prefira `--system`. Checksums dos RPMs usados ficam em
`.circuitalive/dependencies.sha256`, com uma cópia da preparação inicial em
[FEDORA44_RPMS.sha256](FEDORA44_RPMS.sha256); as versões detectadas ficam no log do CMake.
Os pacotes são extraídos sem executar scripts RPM e sem instalação global.
Antes de configurar, os scripts também reparam atalhos locais de bibliotecas
que tenham ficado inválidos após atualizações do sistema, mantendo o mesmo nome de ABI.

## Compilar e instalar

Execute a partir da raiz:

```bash
tools/circuitalive/configure.sh
tools/circuitalive/build.sh
tools/circuitalive/install.sh
```

`configure.sh` seleciona `circuitalive-fedora-local` quando encontra as
dependências locais; caso contrário, seleciona `circuitalive-fedora`. Os presets
estão em `CMakePresets.json` e usam Ninja, `RelWithDebInfo`, `-O2 -g1`, PCH e
ccache. O limite de ccache é 5 GB. Os comandos de link são serializados para
reduzir picos de memória.
O rastreamento adicional de dependências pelo linker é desativado: o linker GNU
não escapou os espaços nos caminhos de bibliotecas importadas nesta máquina.
As dependências explícitas do CMake continuam sendo usadas pelo Ninja.

A compilação usa três tarefas simultâneas por padrão, adequadas à memória
disponível nesta máquina. Para ajustar:

```bash
CIRCUITALIVE_JOBS=2 tools/circuitalive/build.sh
```

O build é incremental: após uma interrupção ou correção, repita o comando.
O script usa um bloqueio para impedir duas compilações simultâneas na mesma árvore.
Para compilar um alvo específico:

```bash
tools/circuitalive/build.sh --target eeschema_kiface eeschema qa_spice
```

`install.sh` instala em `.circuitalive/install`, sem `sudo`. Ele deve ser
executado novamente para atualizar a instalação após recompilar. O preset
`KICAD_SPICE_QA` habilita os testes; o suporte ao simulador e a libngspice são
obrigatórios nesta revisão, que não possui a opção `KICAD_SPICE` no CMake raiz.

## Executar

```bash
./circuitalive
./circuitalive eeschema caminho/circuito.kicad_sch
./circuitalive pcbnew caminho/placa.kicad_pcb
./circuitalive cli version
```

O lançador configura caminhos e bibliotecas. `KICAD_CONFIG_HOME`,
`KICAD_CACHE_HOME` e `KICAD_DOCUMENTS_HOME` apontam para `.circuitalive`.
As tabelas globais de símbolos e footprints são copiadas do Fedora na primeira
execução. `KICAD10_SYMBOL_DIR`, `KICAD10_FOOTPRINT_DIR`, `KICAD10_3DMODEL_DIR`
e `KICAD10_TEMPLATE_DIR` apontam para `/usr/share/kicad`.

Para usar outra coleção, defina `CIRCUITALIVE_LIBRARY_DIR=/caminho/kicad`.
Para executar os binários diretamente do build durante o desenvolvimento:

```bash
./circuitalive --build eeschema demos/simulation/rectifier/rectifier.kicad_sch
```

O `--build` usa `KICAD_RUN_FROM_BUILD_DIR=1`. Os módulos XSPICE do Fedora em
`/usr/lib64/ngspice` são ligados por atalhos junto dos executáveis, em caminhos
que o carregador do KiCad procura. Isso inclui os módulos analógicos, digitais
e `spice2poly` já instalados.

## Verificação

O alvo original `qa_spice` verifica exportação de netlists, modelos e regressões
da integração SPICE, incluindo a execução de análises. Para executar:

```bash
tools/circuitalive/test-spice.sh
```

O teste usa `.circuitalive/test-config`, separado da configuração normal, e
salva o resultado em `.circuitalive/logs/test-spice.log`. Os logs de build e
instalação ficam no mesmo diretório. Os resultados e o escopo da validação inicial
estão em [VALIDATION.md](VALIDATION.md).

## Git

A barra interativa Play/Pause/Stop e o circuito de demonstração estão descritos em
[EDITOR_SIMULATION.md](EDITOR_SIMULATION.md). A integração fica concentrada em
`eeschema/circuitalive/`; esse documento também lista os pontos que precisam ser
revalidados ao atualizar a base KiCad.

```text
origin           git@github.com:ViniciusJesus-art/CircuitAliveEDA.git
upstream         https://github.com/KiCad/kicad-source-mirror.git
upstream-gitlab  https://gitlab.com/kicad/code/kicad.git
```

Branch principal: `main`. O `origin` é um repositório **independente e privado**,
fora da rede de forks do GitHub. Os remotes upstream continuam disponíveis para
consultas e futuras atualizações deliberadas; não acompanhe automaticamente o
HEAD da branch `10.0`. Use a tag registrada em [BASELINE.json](BASELINE.json).

O histórico Git do upstream é mantido, junto com os commits de configuração
reaplicados na base estável. A branch local `circuitalive/main` preserva o estado
anterior à migração para consulta. O fork público anterior é conservado como
arquivo, com o nome `CircuitAliveEDA-upstream-fork`; novos commits do projeto
são enviados somente ao `origin` privado.
O workflow `lockdown.yml` do espelho oficial foi removido: ele fechava e
bloqueava automaticamente pull requests como spam, comportamento inadequado
para o desenvolvimento independente do CircuitAliveEDA.

Os arquivos de `.circuitalive`, inclusive a autenticação local do GitHub, são
ignorados. Os fontes preservam licenças e créditos originais. Referência de
compilação: [documentação oficial do KiCad](https://dev-docs.kicad.org/en/build/getting-started/).
