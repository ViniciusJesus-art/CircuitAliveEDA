# Investigação do runner Windows — 19/10/2026

Estado: diagnóstico e proposta, seguidos de uma auditoria separada autorizada. A auditoria não compila bibliotecas ou aplicativos e mantém o triplet de produção, o rastreamento de compilador e todas as features. As mudanças propostas para uma futura compilação completa ainda não foram aplicadas ao workflow de instalador.

## Evidências da execução

- [Execução 39177604474, job 117823401321](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39177604474/job/117823401321), commit `4a80faafe10fadc55c88474c8e66796a21e3fc4a`.
- Duração do job: **4h27m49s**. Da entrada em `Configure dependencies and applications` até o encerramento: **4h23m54s**. Isso não equivale a tempo comprovado de compilação: o intervalo pode incluir a espera do serviço após a perda da VM.
- Preparação de Visual Studio, vcpkg e SWIG: aprovada. O build dos aplicativos não começou.
- Anotação: `The hosted runner lost communication with the server`. Isso indica perda do agente/VM/conectividade, não identifica um erro do compilador ou prova esgotamento de memória.
- O endpoint de logs do job retorna HTTP 404, `BlobNotFound`. Não há artefatos da execução. As etapas de diagnóstico e de pós-processamento do cache ficaram pendentes.
- A API de caches lista sete entradas, todas do fluxo antigo MSYS2/UCRT64, na referência `release/windows-linux`. **Nenhuma entrada MSVC** foi encontrada. Não são caches aproveitáveis pelo novo build.
- `Restore compiler-tracked vcpkg cache` aparecer como aprovado significa que a ação terminou; não prova `cache-hit`. O workflow não registrava a chave restaurada nem contagens de pacotes reaproveitados.

Os JSONs, respostas HTTP e receitas consultadas estão preservados localmente em `.circuitalive/logs/` e `.circuitalive/vcpkg-audit/`.

## Repositório agora público

A API confirma `private: false`. Para uma nova VM padrão `windows-2022`, o GitHub documenta:

| Repositório | vCPUs | RAM | Armazenamento nominal |
| --- | ---: | ---: | ---: |
| Privado | 2 | 8 GB | 14 GB |
| Público | 4 | 16 GB | 14 GB |

A mudança favorece as próximas execuções. Não demonstra quais recursos foram atribuídos à VM perdida e não aumenta automaticamente o disco. A próxima execução deve registrar os recursos efetivos e o espaço livre de cada volume, sem presumir que os 14 GB são a capacidade física total de C: ou D:.

Fonte: [especificações oficiais dos runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

## Dependências reconstruídas, sem build de pacotes

Foi executado `vcpkg install --dry-run` com o vcpkg fixado em `66c0373dc7fca549e5803087b9487edfe3aca0a1`, os baselines do projeto e as features atuais. Para resolver Windows Desktop no Linux, foi usada uma cópia temporária do triplet com carregamento de vcvars desativado e chainload de auditoria. Houve detecção local de compilador; isso **não valida ABI MSVC ou disponibilidade do cache Windows**. Nenhuma biblioteca/aplicativo foi compilado ou instalado.

O plano contém **133 pacotes distintos**. Inclui muitos componentes Boost somente de cabeçalhos e ferramentas: não significa 133 compilações pesadas. Arquivos: `.circuitalive/vcpkg-audit/windows-install-plan.log` e `resolved-windows-plan.json`.

| Dependência resolvida | Peso esperado e motivo |
| --- | --- |
| OpenCascade 7.9.2 | Alto volume de C++, objetos, bibliotecas e símbolos; pressão de CPU e disco. |
| wxWidgets 3.3.3#2 | GUI completa, com media, sound e webview, além das dependências de imagens. |
| wxPython 4.3.1 | Geração Doxygen/ETG/SIP, C++ dos bindings e preparação Python; candidato a picos de RAM. |
| ICU 78.2, com tools | Bibliotecas e ferramentas de internacionalização; compilação e dados volumosos. |
| Python 3.11.5 | Runtime, módulos de extensão e ferramentas; também há Python auxiliar no port wxPython. |
| Protobuf 3.21.12#4 | Biblioteca e gerador protoc; necessário à API. |
| OpenSSL 3.6.0#3 | Compilação própria e ferramentas auxiliares; tempo adicional no cache frio. |
| ngspice 46, com codemodels | Biblioteca e módulos XSPICE; envolve dois caminhos de helpers MSBuild. |
| Boost 1.90.0#1 e demais transitivas | Muitos ports de cabeçalhos, além dos componentes compilados; há custo de resolução/empacotamento. |

WebView2 1.0.3650.58 também está no plano. A presença do SDK WebView2 não significa compilar Chromium. Não há informação sobrevivente que permita determinar qual desses ports estava ativo no momento da perda do runner ou ordenar seus tempos reais.

## Causas prováveis e problemas confirmados

1. **Build frio amplo: confirmado pelo desenho atual e pela ausência de cache MSVC persistido.** `VCPKG_BINARY_SOURCES=clear;default` limita o fluxo ao cache local da VM; o feed oficial KiCad não era consultado.
2. **Debug e Release das dependências: permitido pela configuração atual.** O triplet não define `VCPKG_BUILD_TYPE`; o padrão vcpkg constrói ambos. `CMAKE_BUILD_TYPE=Release` do aplicativo não altera esse padrão dos ports.
3. **Paralelismo das dependências sem limite explícito: confirmado.** `--parallel 2` só era aplicado às fases de build dos aplicativos. `VCPKG_MAX_CONCURRENCY` não era definido; seu padrão documentado é núcleos lógicos + 1.
4. **Paralelismo aninhado: risco identificado no código.** O helper moderno `vcpkg_msbuild_install` usa `VCPKG_CONCURRENCY`, `MultiProcMaxCount` e `/m:N`. Já `scripts/cmake/vcpkg_build_msbuild.cmake` do checkout fixado contém `/m` sem número. O port ngspice chama esse helper legado para codemodels. Não basta configurar apenas o Ninja ou presumir que toda receita obedece ao limite global.
5. **Pressão de RAM/disco/CPU: hipótese plausível, sem medição.** Fontes extraídas, objetos Debug/Release, instalações, staging e arquivos do cache podem coexistir. Não há pico de memória, contagem de cl.exe ou espaço livre preservados para comprovar OOM ou disco cheio.
6. **Falha da infraestrutura ou rede do GitHub: continua possível.** A anotação não permite excluí-la. O job terminou antes do timeout configurado de 360 minutos.
7. **Recuperação insuficiente: confirmada.** O cache dependia do pós-job; os diagnósticos só seriam enviados após uma falha normal. Perder a VM impediu ambos.

## Melhorias propostas, preservando recursos do CircuitAliveEDA

### 1. Limites iniciais para o runner público

| Camada | Proposta inicial |
| --- | --- |
| vcpkg | `VCPKG_MAX_CONCURRENCY=2` |
| CMake/Ninja | `CMAKE_BUILD_PARALLEL_LEVEL=2` e `--parallel 2` explícito |
| MSBuild | `/m:2` explícito, inclusive nos helpers/ports legados; `MultiProcMaxCount=2` onde suportado |
| Compilador MSVC | `/MP1` para impedir multiplicação interna de workers; manter essa opção no ambiente limpo do vcpkg ou passá-la pela receita |
| Linkedição dos aplicativos | Uma por vez, aproveitando o pool `link=1` existente |

São limites de partida, não uma garantia matemática do número total de processos. Validar pelo monitor se Python/geradores/MSBuild iniciam subprocessos adicionais. Com 4 vCPUs/16 GB, dois workers deixam margem ao agente e à E/S. Aumentar para três somente depois de medir folga de RAM e disco. Não usar variáveis de MSBuild não documentadas como se fossem limites globais.

### 2. Evitar duplicação Debug/Release e controlar disco

- Propor `set(VCPKG_BUILD_TYPE release)` em um triplet de distribuição, tanto para dependências host quanto target aplicáveis, verificando os ports customizados. Isso preserva os recursos do produto; muda a configuração de compilação das bibliotecas.
- Manter MSVC v143, x64, CRT/linkage compatíveis, os mesmos baselines, features e ngspice interno correspondente.
- Usar `--clean-buildtrees-after-build` e `--clean-packages-after-build`: apagar temporários de ports concluídos sem eliminar seus arquivos de cache binário nem os logs superiores. Preservar a árvore do port que falhar.
- Manter cache de downloads separado e limitado. Não remover arquivos indiscriminadamente da imagem do runner.
- Registrar volume de vcpkg/buildtrees, packages, prefixo instalado e cache em checkpoints, evitando varrer toda a árvore a cada amostra.

### 3. Separar plano, dependências e configuração

Propor fases e logs independentes:

1. Inventário do runner/toolchain/cache e início do monitor.
2. `vcpkg install --dry-run`: plano completo salvo, sem build de pacotes.
3. Sondagem opcional `--only-binarycaching`: descobrir os pacotes disponíveis sem iniciar compilações de ports.
4. Instalação explícita vcpkg, com limites e logs incrementais por port.
5. Checkpoint remoto do cache e dos diagnósticos de dependências.
6. CMake com `VCPKG_MANIFEST_MODE=ON`, `VCPKG_MANIFEST_INSTALL=OFF` e `VCPKG_INSTALLED_DIR` apontando para a mesma instalação. O CMake não poderá esconder novas compilações de dependências na configuração.
7. Somente em uma execução de build autorizada: objetos afetados, testes interativos, aplicativos e empacotamento.

Medir início/fim e duração de cada fase, além de `Elapsed time to handle` por port, pacotes restaurados e pacotes construídos. Separar futuramente o job de dependências do job de aplicativos ajuda a não perder dependências prontas se o segundo falhar.

### 4. Monitoramento a cada 30 segundos

Processo de monitoramento separado do processo vcpkg/CMake, iniciado antes das dependências:

- Inventário inicial via CIM: CPU lógica, RAM, SO, volumes, pagefile e versões efetivas de cl, MSBuild, CMake, Ninja, vcpkg e SDK.
- Amostras CSV/JSONL: RAM livre, memória comprometida/limite, CPU total, espaço livre por volume e contagem de processos `cl`, `link`, `msbuild`, `ninja`, `python`, `cmake`, `vcpkg`.
- Para processos de build: PID/PPID, working set, bytes privados e CPU; maiores consumidores. Não despejar todo o ambiente ou credenciais.
- Linha resumida periódica no log Actions. CSV/JSONL gravado incrementalmente, sem depender de um buffer que só é fechado no fim.
- Alertas iniciais propostos: menos de 2 GB de RAM disponível, commit acima de 90%, menos de 5 GB livres nos volumes de trabalho. Ajustar após medir; não são requisitos mínimos oficiais dos ports.
- Interrupção controlada por falta de margem deve afetar apenas a árvore do build, preservando o agente para enviar diagnósticos. Não matar processos globais ou desativar serviços do runner.
- Após falha normal, coletar eventos de esgotamento de recursos do Windows (incluindo Event ID 2004), se disponíveis.

### 5. Cache observável e reaproveitável

- Separar `actions/cache/restore` e `actions/cache/save`; gravar `cache-hit`, chave efetivamente restaurada, número e tamanho dos arquivos.
- Além do sucesso da ação, registrar no log vcpkg quantos pacotes foram realmente restaurados e quantos compilados. Cache descompactado com zero hits ABI não conta como reaproveitamento útil.
- Usar namespace por Windows/x64/v143, manifestos, baselines e triplet, com snapshot por execução e fallback para snapshots compatíveis. Caches Actions são imutáveis; não congelar um primeiro cache parcial em uma chave fixa.
- Manter a verificação de ABI/compilador do vcpkg. Não aproveitar os caches MSYS2 existentes.
- Salvar imediatamente após dependências e também em falhas normais (`if: always()` quando o runner ainda responde). Uma VM perdida não consegue executar esse passo.
- Para sobreviver a uma perda no meio das dependências, considerar armazenamento binário remoto que receba cada port concluído, ou checkpoints entre etapas independentes. Isso exige projeto adicional de persistência; o cache local mais um pós-job não resolve o caso catastrófico.

### 6. Pacotes pré-compilados compatíveis

O KiCad publica um feed NuGet oficial somente de leitura para consumidores. Porém seu triplet oficial deixa o compilador fora dos hashes; o triplet atual mantém rastreamento e tem conteúdo/hash diferente. Simplesmente acrescentar o URL não garante hits.

Proposta: sondar o feed sem compilar (`--only-binarycaching`), com pins, arquitetura, features e ABI preservados; registrar misses. Não desativar rastreamento do compilador apenas para forçar hits. Para adotar o perfil oficial integralmente, primeiro validar a combinação exata de toolchain, triplet, CRT, bibliotecas e módulos customizados do CA. A alternativa mais conservadora é produzir uma vez um cache próprio compatível e restaurá-lo nas próximas execuções.

Não importar DLLs/libs MinGW, não substituir bibliotecas por versões de uma instalação qualquer de KiCad e não desligar wxPython, API, OpenCascade ou simuladores para reduzir o trabalho.

### 7. Diagnósticos que sobrevivam tanto quanto possível

- Preservar plano, log de instalação vcpkg, logs superiores e ABI info dos ports, erros de configuração, CMakeConfigureLog, binlogs MSBuild quando disponíveis, inventário e métricas.
- Logs incrementais da instalação explícita evitam depender apenas de `vcpkg-manifest-install.log`, que a integração CMake escreve depois que o processo vcpkg retorna.
- Enviar artefatos nos checkpoints, antes dos aplicativos, e em `always()` nas saídas normais. Para cobrir perda abrupta da VM durante uma fase longa, considerar envio remoto periódico de snapshots em lotes; salvar somente no disco local não basta.
- Nunca prometer recuperação total após `lost communication`: a VM pode desaparecer antes de qualquer upload. Registrar explicitamente os limites dos dados recuperados.

## Próxima execução proposta

Primeiro uma execução de auditoria/cache, sem build dos aplicativos: inventário + monitor + plano + prova de restauração de binários. Depois, caso necessário e autorizado, preparar apenas as dependências com os limites e checkpoints. A compilação completa continua manual e não foi iniciada nesta investigação.

## Auditoria implementada

O workflow manual `.github/workflows/windows-audit.yml` usa `windows-2022` e compartilha o grupo de concorrência do instalador, impedindo que esses dois jobs disputem execução simultaneamente na mesma referência. Não chama configuração CMake, build ou empacotamento.

- Inventário real de CPU, RAM, volumes, pagefile, imagem, compilador e ferramentas.
- Monitor CIM independente a cada 30 segundos durante cada operação longa: memória física/commit, CPU, discos e processos de compilação com PID/PPID e consumo. O processo pai permanece vivo durante a operação, e `finally` encerra o monitor; cada amostra JSONL é fechada imediatamente. Há também uma amostra síncrona inicial.
- Plano nativo separado (`--dry-run`), sondagem do cache local e sondagem somente de leitura do feed KiCad com `--only-binarycaching --keep-going`. Este último flag proíbe fallback para compilação de ports. O vcpkg ainda pode executar a pequena detecção de compilador/ABI.
- Limites de dois workers para vcpkg/CMake durante a auditoria e `/MP1` no ambiente MSVC. Como a auditoria não compila ports, ela não comprova ainda a eficácia desses limites nos helpers legados.
- Restauração explícita do cache Actions, registro de chave/hit e inventário antes/depois. Pacotes ausentes são resultados da sondagem; erros de configuração/rede são falhas.
- Checkpoints remotos de inventário e plano, além da coleta final com `always()`. Binários eventualmente obtidos são salvos se houver arquivos de cache. Não há publicação de release.
- Testes offline verificam os argumentos que proíbem builds, interpretação de hits/misses/erros, inventário de cache e inicialização do ambiente sem operações de compilação.

Os resultados da execução devem ser lidos nos artefatos `windows-audit-inventory`, `windows-audit-plan` e `windows-audit-diagnostics`. O sucesso do workflow de auditoria não significa que todas as dependências estejam disponíveis em cache: consultar `*-result.json` e `summary.md`.

### Primeira medição no runner público

[Auditoria 39233128984](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39233128984), commit `1a1344ccf7`:

- Windows Server 2022, imagem `win22`/`20261012.333.1`, **4 CPUs lógicas, 15,99 GiB de RAM**. Espaço livre inicial: **82,66 GiB em C:** e **145,62 GiB em D:**. Pagefile alocado em D: de 2944 MiB.
- MSVC 19.44.35229, toolset 14.44.35207, MSBuild 17.14.60, SDK 10.0.26100.0, CMake 3.31.6, Ninja 1.13.2; vcpkg fixado conforme lock.
- Plano nativo concluído em **44,8 s**, com os mesmos **133 pacotes**. Isso valida a resolução Windows, não a compilação das bibliotecas/aplicativos.
- Cache Actions sem chave correspondente (`cacheHit` e `matchedKey` vazios); cache binário local com zero arquivos/pacotes. Sondagem local: **0 restaurados, 133 ausentes**, 8,2 s.
- Feed oficial acessível: **133 requisições `.nupkg`, 133 respostas `NotFound`, zero restaurados**, 11,6 s. Não há pacotes para os hashes ABI solicitados; não significa que o feed inteiro esteja vazio. Não houve erro de autenticação ou transporte identificado nesse log.
- Inventário, plano e diagnósticos finais foram enviados e baixados em `.circuitalive/reports/windows-audit-39233128984/`.

Essa primeira execução revelou duas falhas na instrumentação: o parser confundiu o aviso agregado do NuGet (exit 1 por pacotes ausentes) com erro real, e o monitor iniciado em um step anterior só deixou uma amostra. O parser foi corrigido para aceitar apenas o aviso específico quando todas as requisições têm respostas `OK`/`NotFound`, mantendo falhas de rede/autenticação como erro. O monitor passou a acompanhar cada operação longa dentro do tempo de vida de seu pai. A amostra inicial tinha 12,85 GiB livres, mas **não permite concluir picos de RAM/CPU dessa execução**.

Continua necessário criar cache próprio ou validar integralmente um perfil oficial compatível antes de esperar reaproveitamento de binários. Não houve build de bibliotecas ou aplicativos nesta auditoria, nem publicação de release.

### Auditoria corrigida aprovada

[Execução 39233966933](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39233966933), commit `4f33cfd092`: **sucesso em 2min36s**. Todos os testes e fases passaram; nenhum build de port/aplicativo foi iniciado.

| Medição | Resultado |
| --- | --- |
| Runner | 4 CPUs lógicas, 15,99 GiB RAM |
| Plano nativo | 133 pacotes, 46,9 s |
| Cache local | 0 restaurados, 133 ausentes, 9,1 s |
| Feed oficial, ABI atual | 0 restaurados, 133 ausentes, 14,2 s |
| Amostras de recursos | 6: inventário, setup, duas no plano, cache local e feed |
| RAM livre mínima amostrada | 12,75 GiB |
| CPU máxima amostrada | 30% |
| Commit máximo amostrado | 15,14% do limite |
| Espaço livre mínimo amostrado | C: 82,80 GiB; D: 145,21 GiB |
| Processos de ferramentas de build simultâneos nas amostras | Máximo 1 |

Os artefatos foram baixados em `.circuitalive/reports/windows-audit-39233966933/`. A validação local confirmou amostras do plano e o resultado `cache-miss` das duas sondagens, com 133 ausentes em cada uma. A gravação de cache foi corretamente pulada porque nenhum binário foi obtido.

Esses números medem resolução e sondagem, **não a carga de compilar OpenCascade/wxPython/etc.** Não comprovam a causa da perda da VM anterior nem permitem aumentar o paralelismo com segurança. Confirmam um perfil de dependências totalmente frio neste runner e que o simples acesso ao feed oficial não fornece os binários do ABI atual.

Próxima etapa recomendada: aplicar os limites também aos caminhos MSBuild legados, separar a instalação explícita de dependências da configuração CMake e planejar a persistência do cache próprio. Só então iniciar uma preparação de dependências autorizada, com checkpoints; a compilação completa permanece manual e parada.

## Referências

- [Execução investigada](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39177604474).
- [Recursos dos runners públicos e privados](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
- [VCPKG_MAX_CONCURRENCY e ambiente](https://learn.microsoft.com/en-us/vcpkg/users/config-environment).
- [Triplets: Release e rastreamento de compilador](https://learn.microsoft.com/en-us/vcpkg/users/triplets).
- [vcpkg install: dry-run, only-binarycaching e limpeza](https://learn.microsoft.com/en-us/vcpkg/commands/install).
- [Cache binário e hashes ABI](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching).
- [Cache Actions: hits, imutabilidade e salvamento](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching).
- [MSBuild: /m e contagem de processos](https://learn.microsoft.com/en-us/visualstudio/msbuild/msbuild-command-line-reference).
- [MSVC: /MP](https://learn.microsoft.com/en-us/cpp/build/reference/mp-build-with-multiple-processes).
- [KiCad: configuração Windows e feed oficial](https://dev-docs.kicad.org/en/build/windows-msvc/).
