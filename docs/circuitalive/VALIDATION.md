# Validação da base estável

Registro da etapa 0, anterior à implementação da sessão interativa. A validação
da etapa 1 está em [INTERACTIVE_SESSION.md](INTERACTIVE_SESSION.md).

Executada em 16/10/2026, no Fedora Linux 44 x86_64 desta máquina.

## Base para o desenvolvimento

- KiCad **10.0.7**, tag estável oficial de 06/10/2026.
- Commit upstream: `69cafb907ebc09e7722a2a464b1d466fc2e3608d`.
- Registro da base: [BASELINE.json](BASELINE.json).
- Branch principal: `main`.
- Repositório independente privado: <https://github.com/ViniciusJesus-art/CircuitAliveEDA>.
- Fontes diretamente na raiz de `CircuitAliveEDA`.
- Histórico upstream completo: 56.249 commits até a tag estável.
- Nenhuma alteração em fontes nativas do KiCad em relação à tag.

Os scripts, presets, lançador e documentação do CircuitAliveEDA foram
reaplicados sobre a tag estável. A revisão pós-release do mapa original foi
substituída. O registro da primeira preparação permanece em
[VALIDATION_INITIAL.md](VALIDATION_INITIAL.md), como histórico.

O binário identifica a base `10.0.7`, seguida da quantidade e do hash dos commits
de preparação do projeto e do sufixo `CircuitAliveEDA`. Esses commits não
introduzem código upstream posterior à versão estável. O hash exato do binário
instalado está em `.circuitalive/logs/version-stable-commit.txt`.

## Ambiente

| Componente | Versão |
|---|---|
| GCC / padrão | 16.2.1 / C++20 |
| CMake / Ninja | 4.3.0 / 1.13.2 |
| wxWidgets / wxPython | 3.2.9 / 4.2.4 |
| Boost / OpenCascade | 1.90.0 / 7.9.3 |
| Python / SWIG | 3.14.7 / 4.4.1 |
| libngspice / code models | 47 / 47 |

As dependências e bibliotecas de componentes preparadas anteriormente foram
reutilizadas. Não foi necessária instalação global nem patch no código nativo
para compilar a versão estável nesta máquina.

## Resultados

| Verificação executada | Resultado |
|---|---|
| Build completo: editores, CLI, 3D e executáveis QA | Aprovado |
| Instalação local em `.circuitalive/install` | Aprovado |
| CLI instalada: versão e hash | Base 10.0.7, ngspice 47 |
| Suíte original `qa_spice` | 53 casos aprovados, zero falhas |
| Exportação de netlist SPICE pela CLI instalada | Aprovado; caminho do modelo com espaços preservado entre aspas |
| Eeschema instalado: abertura do retificador | Aprovado |
| Simulação gráfica `.tran 1u 10m` | 10.008 pontos; curvas de entrada e saída exibidas |
| Encerramento normal do editor com o simulador aberto | Exit code 0 |
| Ancestralidade e diferença de fontes em relação à tag | Base confirmada; zero alterações nativas |

O teste gráfico usou Xvfb com renderização por software, configuração local do
projeto e uma cópia do exemplo em `.circuitalive/smoke/stable-10.0.7`. A tela
virtual e os aplicativos de teste foram encerrados após a validação.

Os executáveis das demais suítes QA foram compilados; a suíte automatizada
executada foi `qa_spice`. A implementação dos novos controles de simulação
interativa pertence às próximas etapas.

## Repositório

O GitHub confirma `private: true` e `fork: false` para o novo CircuitAliveEDA.
Uma consulta à API sem autenticação retorna HTTP 404. A tag `10.0.7` e a branch
`main` são publicadas nesse repositório, preservando o histórico do upstream.
Os remotes `upstream` e `upstream-gitlab` continuam disponíveis para consulta.

O fork público anterior foi renomeado para
[CircuitAliveEDA-upstream-fork](https://github.com/ViniciusJesus-art/CircuitAliveEDA-upstream-fork)
e arquivado; ele permanece público como registro anterior. Novos commits do
projeto são enviados ao repositório privado. O workflow do espelho oficial que
fechava pull requests como spam foi removido da nova branch principal.

## Evidências locais

Em `.circuitalive/logs/`, ignorado pelo Git:

- `configure-stable-10.0.7.log`, `build-stable-10.0.7.log`: configuração e build completo.
- `build-stable-refresh.log`: atualização dos metadados e links finais.
- `build-stable-final.log`: build final após registrar esta documentação.
- `install-stable-10.0.7.log`: instalação local.
- `version-stable-about.txt`, `version-stable-commit.txt`: identificação dos binários.
- `test-spice-stable-10.0.7.log`: CTest com os 53 casos SPICE.
- `stable-source-audit.json`: ancestralidade e comparação com a tag.
- `rectifier-stable-10.0.7.cir`: netlist exportada pela CLI.
- `eeschema-stable-10.0.7.png`, `rectifier-simulation-stable-10.0.7.png`: capturas gráficas.
- `eeschema-stable-10.0.7.log`: execução gráfica.
- `repository-private.json`, `repository-archived-fork.json`: estado dos repositórios.

Comandos de preparação e execução: [BUILD_FEDORA.md](BUILD_FEDORA.md).
