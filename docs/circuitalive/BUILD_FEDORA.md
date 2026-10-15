# CircuitAliveEDA: base e compilação no Fedora

## Escopo

Esta é a etapa 0 do [mapa](Mapa_KiCad_Simulacao_Interativa.md): fork, checkout
completo, ambiente de compilação, instalação local e verificação do KiCad original.
A implementação de Play/Pause/Stop e de simulação interativa ainda pertence às
etapas seguintes.

Base fixada: [6809ada8746084720c0093eb484c889afb35d0d4](https://github.com/KiCad/kicad-source-mirror/tree/6809ada8746084720c0093eb484c889afb35d0d4),
branch upstream `10.0`. O código declara `10.0.7-unknown` como versão de
desenvolvimento; o sufixo de compilação `CircuitAliveEDA` identifica este projeto.
Um checkout raso sem tags pode apresentar `unknown` na versão, mesmo com o hash
exato disponível no binário.

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
desabilitadas para este fork.

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
`.circuitalive/dependencies.sha256`; as versões detectadas ficam no log do CMake.
Os pacotes são extraídos sem executar scripts RPM e sem instalação global.

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

A compilação usa três tarefas simultâneas por padrão, adequadas à memória
disponível nesta máquina. Para ajustar:

```bash
CIRCUITALIVE_JOBS=2 tools/circuitalive/build.sh
```

O build é incremental: após uma interrupção ou correção, repita o comando.
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
instalação ficam no mesmo diretório. Resultados e limitações da validação inicial
serão registrados em `VALIDATION.md` após a execução.

## Git

```text
origin           git@github.com:ViniciusJesus-art/CircuitAliveEDA.git
upstream         https://github.com/KiCad/kicad-source-mirror.git
upstream-gitlab  https://gitlab.com/kicad/code/kicad.git
```

Branch de trabalho: `circuitalive/main`. A branch local `upstream/10.0-base`
preserva a revisão escolhida. O download inicial usa histórico raso, com todos
os arquivos de trabalho presentes; o fork remoto mantém o histórico upstream.
Se precisar do histórico completo local:

```bash
git fetch --unshallow upstream
```

Os arquivos de `.circuitalive`, inclusive a autenticação local do GitHub, são
ignorados. Os fontes preservam licenças e créditos originais. Referência de
compilação: [documentação oficial do KiCad](https://dev-docs.kicad.org/en/build/getting-started/).
