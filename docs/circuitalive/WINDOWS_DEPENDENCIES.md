# Preparação das dependências Windows MSVC

O workflow manual `windows-dependencies.yml` prepara somente as bibliotecas e ferramentas do manifesto. Não configura nem compila os aplicativos e não cria instaladores ou releases. Usa o runner público `windows-2022` e o mesmo grupo de concorrência dos workflows Windows existentes.

## Perfil de distribuição

- MSVC v143, x64, bibliotecas/CRT dinâmicos conforme o triplet; exceções próprias dos ports oficiais, como a DLL autocontida do ngspice, são preservadas.
- `VCPKG_BUILD_TYPE=release`: elimina a segunda compilação Debug das dependências. Mantém todas as features, inclusive `wxWidgets[debug-support]`, que habilita hooks também em Release.
- `VCPKG_MAX_CONCURRENCY=2`, `CMAKE_BUILD_PARALLEL_LEVEL=2` e `_CL_=/MP1`. A variável `_CL_` atravessa o ambiente limpo do vcpkg e participa do hash ABI; o MSVC a aplica depois dos argumentos da linha de comando.
- `MSYS2_ENV_CONV_EXCL=_CL_`, também preservada e rastreada pelo vcpkg, impede que o shell de ports Autotools transforme `/MP1` em um caminho ao iniciar `cl.exe`.
- `VCPKG_BINARY_SOURCES=clear;default,readwrite`: leitura e gravação explícitas. `clear;default` sozinho é somente leitura; não basta salvar uma pasta vazia com Actions/cache.
- O helper legado `vcpkg_build_msbuild` passa de `/m` para `/m:${VCPKG_CONCURRENCY}`. A alteração valida o SHA-256 do arquivo original fixado, aceita reaplicação e rejeita conteúdo desconhecido. O script dessa alteração também participa do hash ABI. O helper moderno já limita MSBuild e processos compartilhados pelo `VCPKG_CONCURRENCY`.
- Rastreamento de compilador permanece habilitado. Não há reutilização de DLLs/arquivos `.lib` MinGW ou de binários do feed KiCad com hashes incompatíveis.

O perfil Release tem hashes diferentes do perfil Debug+Release auditado anteriormente. O cache anterior, que estava vazio, não é tratado como compatível. As receitas wxPython podem adquirir bash/MSYS como ferramenta de processamento; o compilador das bibliotecas continua MSVC.

## Etapas e persistência

As quatro etapas usam manifestos cumulativos derivados do manifesto original, preservando registries, versões fixadas e objetos de dependência/features. A instalação final tem o manifesto completo, semanticamente idêntico ao original.

| Etapa | Acréscimos | Timeout do step |
| --- | --- | --- |
| foundation | Bibliotecas básicas, Python, Protobuf, ICU e demais raízes fora dos grupos seguintes | 100 min |
| simulation | ngspice com codemodels/XSPICE | 40 min |
| cad | OpenCascade e suas features atuais | 70 min |
| complete | wxWidgets e wxPython com todas as features do manifesto | 110 min |

Antes dessas quatro etapas, um preflight prepara apenas `gperf` e suas próprias dependências. Um teste nativo usa o shell MSYS2 adquirido pelo port, ou um shell MSYS instalado no runner quando o cache dispensa downloads de ferramentas de build, para verificar os flags e compilar/executar um pequeno programa C. Em seguida, prova gravação de ZIPs e restauração vcpkg com `--only-binarycaching` para uma instalação nova. Nenhuma etapa pesada começa se essas verificações falharem. O input manual `preflight_only=true` encerra o workflow após esse diagnóstico.

O job tem limite de 360 minutos. Timeout de step deixa margem para coleta final se o agente ainda responder. Cada instalação grava log incremental, argumentos, duração, resultado, inventário do cache e métricas de RAM/commit, CPU, disco e processos a cada 30 segundos. O monitor é encerrado em `finally`; o processo pai permanece vivo durante a instalação.

Depois do preflight, foundation, simulation e cad, há upload dos diagnósticos e salvamento explícito do cache binário. Um checkpoint final usa `always()` e preserva pacotes concluídos também em falhas normais. O cache usa namespace por perfil e snapshots imutáveis por execução/etapa, com fallback para o snapshot compatível mais recente. Os workflows de auditoria e instalador compartilham esse namespace.

Cache Actions restaurado não significa cache ABI utilizado: conferir `actions-cache.json`, `*-install-result.json`, mensagens `Restored` do vcpkg e inventários `*-cache.json`. O cache pode sofrer evicção pelas regras/cotas do GitHub; os snapshots são cumulativos. Não existe promessa de recuperar pacotes concluídos desde o último checkpoint se a VM desaparecer. Os checkpoints já enviados sobrevivem, enquanto logs/cache ainda só no disco da VM podem ser perdidos.

Os flags `--clean-buildtrees-after-build` e `--clean-packages-after-build` removem fontes/objetos temporários de ports concluídos, mantendo os logs superiores e o cache binário. O port que falhar mantém sua árvore para diagnóstico; a coleta copia logs/ABI de tamanho limitado, sem arquivar objetos ou fontes inteiras.

## Uso local e separação do CMake

Em PowerShell nativo, com Visual Studio 2022 C++ instalado:

```powershell
./tools/circuitalive/release/build-windows.ps1 -Phase setup
./tools/circuitalive/release/prepare-windows-dependencies.ps1 -Stage all -Plan
./tools/circuitalive/release/build-windows.ps1 -Phase dependencies
```

`-Plan` não instala ou compila ports, embora o vcpkg possa executar a pequena detecção de compilador/ABI. Para executar uma etapa isoladamente, passar `-Stage foundation`, `simulation`, `cad` ou `complete`.

Somente após a instalação completa bem-sucedida é criado `build/windows-msvc/dependencies-complete.json`, associado ao perfil. O script de configuração verifica esse comprovante e o banco da instalação. Alterar manifesto, pins, triplet ou limites invalida o comprovante.

O preset CMake tem `VCPKG_MANIFEST_INSTALL=OFF` e usa a mesma instalação `build/windows-msvc/vcpkg_installed`. Configurar o aplicativo não inicia outra compilação oculta de dependências. O workflow de instalador também ganhou uma fase explícita de dependências, mas continua exclusivamente manual e não é disparado pelo workflow de dependências.

## Próxima etapa: validar os fontes com MSVC

A [preparação 39241396473](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39241396473) concluiu todas as etapas em 1h24min20s, incluindo ngspice/XSPICE, OpenCascade, wxWidgets e wxPython. O checkpoint final contém 133 pacotes binários; as amostras registraram no mínimo 11,53 GiB de RAM livre.

O workflow manual `windows-validate.yml` restaura esse mesmo perfil de cache e verifica todos os pacotes com `--only-binarycaching`. Se faltar algum pacote compatível, interrompe antes do CMake para que a preparação seja tratada separadamente. Como cada runner começa com uma instalação vazia, instala o manifesto completo a partir do cache e gera novamente o comprovante de conclusão.

Em seguida, executa separadamente `-Phase configure` e `validate-windows-sources.ps1`, com dois jobs e `/MP1`. O segundo script compila `qa_interactive_sim`, suas dependências (incluindo o worker) e os objetos da interface CA e consumidores de branding descritos abaixo; só depois chama `-Phase validate` para preparar o runtime e executar os testes do simulador interativo. As duas fases registram transcrições e recursos a cada 30 segundos, com upload final dos logs CMake/CTest e métricas mesmo em falhas normais. Esse workflow não chama `-Phase build`, não compila todos os aplicativos e não gera instalador ou release.

A [validação 39253280301](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39253280301) restaurou os 133 pacotes em 61,1 segundos e configurou o CMake com sucesso. A compilação parou no erro MSVC C2445 em `LibraryTypeId`, no cabeçalho `include/circuitalive/branding.h`: a expressão condicional entre um literal `wchar_t` e `wxString` tinha duas conversões possíveis. Retornos separados eliminam a ambiguidade e preservam o token `KiCad` dos arquivos. Durante a compilação, as amostras registraram pelo menos 12,19 GiB de RAM livre.

Antes dos alvos maiores, o workflow agora verifica esse cabeçalho com `cl /Zs` e os headers wxWidgets da instalação MSVC. A saída dos processos nativos também passa explicitamente por `Tee-Object`: a transcrição PowerShell da primeira execução não reteve as mensagens do compilador; o log completo foi recuperado do Actions. Os diagnósticos dessa execução estão em `.circuitalive/reports/windows-validation-39253280301/`.

A [validação 39256144749](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39256144749) passou pela checagem de branding e avançou até o módulo XSPICE de memória. Falhou em `cktdefs.h`, que inclui `../maths/dense/dense.h` quando RFSPICE está ativo. A receita fixada do ngspice 46 copia `src/include` e a configuração Visual C++, mas omite `src/maths/dense/dense.h`. Desativar RFSPICE mudaria o layout ABI e não é uma solução válida.

`cmake/CircuitAliveNgspiceHeaders.cmake` completa apenas esse include privado em uma pasta do build. O arquivo existente foi comparado com a revisão exata do port e é idêntico; a configuração valida versão 46 e SHA-256 normalizado, preserva os headers e flags do pacote e não expõe os headers ABI da versão 47 ao MSVC. O perfil das dependências e seus ZIPs continuam os mesmos. O workflow agora compila e vincula `circuitalive-memory` inteiro antes das bibliotecas comuns, permitindo diagnosticar rapidamente qualquer outro problema nessa integração. Os logs dessa falha estão em `.circuitalive/reports/windows-validation-39256144749/`; as amostras da compilação registraram no mínimo 12,44 GiB de RAM livre.

### Revisão preventiva e coleta de erros independentes

A [validação 39268033043](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39268033043) compilou e vinculou o módulo XSPICE completo com MSVC, mas parou depois em outra expressão texto/`wxString`, no gerenciador de repositórios. A correção torna explícito o `wxString` do nome padrão. A revisão de condicionais nos fontes alterados identificou mais quatro expressões no visualizador de memória e osciloscópio, agora com tipos explícitos nos dois ramos. Também foram revisados enums, `NOMINMAX` e os ramos Windows/POSIX do runtime. A função CA `ReplaceFile` passou a `ReplaceSavedFile`, evitando a macro homônima do Windows; o teste de ordem de headers exercita essa macro, `ERROR` e os enums do osciloscópio.

`validate-windows-sources.ps1` usa o `compile_commands.json` gerado pelo preset para selecionar todos os objetos C++ de `eeschema/circuitalive`, `common/circuitalive`, o runtime de plataforma, o gerenciador de repositórios e os consumidores diretos de `branding.h`. No banco MSVC da execução anterior, isso corresponde a 68 fontes e 73 objetos, incluindo consumidores do esquemático, PCB e outros editores. `-Plan` permite revisar a seleção sem compilar. O script rejeita saídas absolutas, caminhos que escapem do build e alvos que não sejam `.obj`; não solicita alvos de aplicativos completos.

A compilação usa Ninja `-k 0`: continua os objetos independentes após uma falha, mas mantém o resultado de erro e não executa os testes até todos os alvos selecionados passarem. Objetos que dependem de uma etapa com erro ainda ficam bloqueados. `source-plan.json` registra os arquivos, objetos e comando; `compile.log` e `compile-result.json` preservam a saída completa e os diagnósticos destacados. O limite de dois jobs e o perfil binário das dependências permanecem iguais.

A [validação 39277359494](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39277359494) concluiu a configuração em 22 segundos e percorreu as 938 tarefas da compilação ampliada em cerca de 22 minutos. Os diagnósticos agruparam duas causas: `branding.h` dependia de um include indireto de `wxBitmap`, e `editor_simulation.h` ainda fornecia stubs fora do Linux, conflitando com as seis implementações reais no Windows. O header agora inclui `wx/bitmap.h` explicitamente e habilita as declarações reais também sob `_WIN32`, conforme o CMake já fazia. A checagem inicial deixou de incluir `wx/wx.h`, que mascarava o include ausente, e define as seis funções em uma unidade compilada somente com `/Zs`, para detectar regressões dos stubs antes da compilação maior.

### Histerese do clock no ngspice 46

A [tentativa 2 da validação 39283417390](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39283417390/attempts/2) compilou os fontes selecionados e chegou aos 178 casos do simulador. As duas asserções com falha pertenciam a `PipelineThresholdRippleIsOneClockEdgeUntilClockReturnsLow`, nas variantes HC e HCT: Q2 subia após uma oscilação dentro da faixa de histerese.

O `adc_bridge` do [ngspice 46](https://github.com/imr/ngspice/blob/ngspice-46/src/xspice/icm/digital/adc_bridge/cfunc.mod) não retém o estado quando `in_low > in_high`, ao contrário da versão 47 usada localmente. O compilador de modelos agora usa comparadores dos limiares e um latch SR, com ADCs em ordem crescente. Os testes e as dependências Windows continuam os mesmos.

A reprodução local isolou o conversor original da versão 46, mantendo a ABI do runtime local: o compilador de modelos anterior reproduziu exatamente as duas asserções e os valores de Q2 próximos a 5 V observados no Actions. Com a correção, os cinco casos de pipeline passaram tanto nesse conversor quanto no runtime 47; a suíte completa de 178 casos também passou no runtime 47. Essa reprodução verifica o comportamento do ADC; a validação nativa MSVC continua necessária.

## Validação

Os testes offline verificam manifestos cumulativos e finais, pins/registries/features, argumentos, rejeição de preparação ausente/desatualizada, conteúdo conhecido e reaplicação do patch. Após o setup, um teste executa o helper CMake real com o processo MSBuild substituído por um verificador: comprova `/m:2` e somente Release antes de compilar qualquer port. Nenhum compilador é executado nesse teste.

### Falha gperf e correção comprovada no Windows

A [execução 39236685543](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39236685543) falhou no `gperf` durante a configuração Autotools (exit 77, `C compiler cannot create executables`). Os pacotes anteriores não geraram cache porque a configuração `clear;default` permitia apenas leitura. O coletor original também excluía `x64-windows-rel/lib/config.log`; isso foi corrigido para preservar logs internos, mantendo objetos/fontes fora da coleta.

O [preflight 39240310569](https://github.com/ViniciusJesus-art/CircuitAliveEDA/actions/runs/39240310569), commit `ab9dce02d2`, **passou em 4min58s** e comprovou:

- Sem proteção, o shell MSYS2 adquirido pelo port transforma `_CL_=/MP1` em `D:/ca-tools/vcpkg/downloads/tools/msys2/19f68f647e350a23/MP1`. O teste nativo reproduz `LNK1181` tentando abrir `MP1.obj`, com exit 2.
- Com `MSYS2_ENV_CONV_EXCL=_CL_`, o compilador recebe `/MP1` intacto; compilação e execução do pequeno programa C retornam zero.
- `gperf` e suas três dependências concluem com sucesso, em 150,9 segundos.
- O cache com `readwrite` contém **4 ZIPs, 186591 bytes**. Uma instalação nova restaura os quatro pacotes em 102 ms, usando `--only-binarycaching`, sem fallback para compilação.
- A API Actions confirma snapshots `preflight` e `final` salvos remotamente para o perfil `0a5e80f7c79251e1c1629b5b3106ffd38fb1a2a20e5df4107280f67f393496ad`, com 179950 bytes comprimidos cada.

Os diagnósticos foram baixados em `.circuitalive/reports/windows-preflight-39240310569/`. Nenhum aplicativo foi compilado nessa validação. A aprovação do preflight valida o port que falhou e o mecanismo de cache; as demais dependências continuam sendo verificadas na preparação subsequente.

Referências: [triplets e ambiente limpo do vcpkg](https://learn.microsoft.com/en-us/vcpkg/users/triplets), [limpeza e instalação explícita](https://learn.microsoft.com/en-us/vcpkg/commands/install), [MSVC `/MP`](https://learn.microsoft.com/en-us/cpp/build/reference/mp-build-with-multiple-processes), [modos de leitura/gravação do cache](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching), [conversão de variáveis no MSYS2](https://www.msys2.org/docs/filesystem-paths/).
