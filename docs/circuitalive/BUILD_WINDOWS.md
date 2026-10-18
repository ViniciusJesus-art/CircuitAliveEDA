# Compilação Windows x64 com MSVC

O CircuitAliveEDA usa Visual Studio 2022 (MSVC v143), CMake/Ninja e vcpkg em modo manifesto. O diretório `build/windows-msvc` é independente dos builds Linux e MSYS2. Bibliotecas MinGW (`.dll.a`, libstdc++, libgcc) não entram no build ou no instalador MSVC.

## Referências oficiais consultadas em 18/10/2026

- [KiCad: Windows/Visual Studio](https://dev-docs.kicad.org/en/build/windows-msvc/), incluindo a seção específica do branch 10.0: SWIG e Python ainda são necessários.
- [KiCad: ferramentas e dependências](https://dev-docs.kicad.org/en/build/getting-started/).
- [KiCad Windows builder](https://gitlab.com/kicad/packaging/kicad-win-builder): MSVC, Ninja, vcpkg e empacotamento NSIS/MSIX. O script oficial é uma referência de distribuição; o fork mantém sua marca, launcher e validações próprias.
- [Port ngspice do registro KiCad](https://gitlab.com/kicad/packaging/kicad-vcpkg-registry/-/tree/1c729aa1d25750bd43f392a6ae618ea96d6ee76c/ports/ngspice): compila a biblioteca e os code models com Visual C++.

`windows-toolchain.json` fixa o vcpkg utilizado pelo builder oficial e SWIG 4.3.1. Os baselines existentes de `vcpkg-configuration.json` e os overrides de `vcpkg.json` continuam fixos: wxWidgets 3.3.3, wxPython 4.3.1, Python 3.11.5, ngspice 46 e Protobuf 3.21.12, além das demais dependências do manifesto (Boost, OpenCascade, Cairo, curl, OpenSSL, SQLite, ICU, libgit2, nng, zstd etc.). A versão do executável `protoc` deve acompanhar a biblioteca Protobuf do mesmo manifesto. O ZIP do SWIG usa o SHA-256 fixado pelo builder oficial; páginas HTML retornadas pelo SourceForge são rejeitadas antes da extração, com tentativa de outros espelhos.

O triplet próprio mantém a verificação do compilador no hash de ABI do vcpkg e fixa v143. O fluxo usa somente o cache local compatível, recompilando dependências quando necessário; não importa o cache oficial que desativa essa verificação. O port oficial ngspice usa CRT estática por ser uma DLL independente; os outros componentes seguem o triplet com CRT dinâmica. O módulo de memória utiliza os cabeçalhos internos e `config.h` da mesma instalação ngspice, incluindo OSDI, CIDER e KLU, e detecta a disponibilidade da interface de ruído adicionada na versão 47.

## Execução local

Instale o Visual Studio 2022 com **Desktop development with C++**, Windows SDK e ferramentas CMake/Ninja. Instale Git, PowerShell 7 e NSIS. Use caminhos curtos e habilite caminhos longos no Windows se necessário.

Em PowerShell nativo, na raiz do checkout:

```powershell
$env:CA_WINDOWS_TOOLS = 'C:/ca-tools'
./tools/circuitalive/release/build-windows.ps1 -Phase setup
./tools/circuitalive/release/build-windows.ps1 -Phase configure -Version 10.0.7-alpha.1
./tools/circuitalive/release/build-windows.ps1 -Phase build
./tools/circuitalive/release/prepare-windows-libraries.ps1
./tools/circuitalive/release/build-windows.ps1 -Phase package -Version 10.0.7-alpha.1
```

A fase `build` primeiro compila o worker, controlador, módulo de memória, teste com `windows.h` incluído antes dos cabeçalhos do simulador e os consumidores GUI afetados. Executa então `qa_interactive_sim`; uma falha interrompe o script antes da compilação completa. Para executar somente essa validação, use `-Phase validate` após configurar.

A compilação preserva API IPC, wxPython/SWIG, OpenCascade, editor esquemático, editor PCB, simulador clássico, worker interativo, módulo de memória, modelos, blocos, traduções e demos. `SIM_SESSION_STATE::FAILED` e `EDITOR_SESSION_STATE::FAILED` coexistem com a macro Win32 `ERROR`; no protocolo, o estado continua sendo `"error"`, versão 2. `NOMINMAX` vale para todos os builds Windows.

O instalador NSIS inclui Python/wxPython, DLLs MSVC redistribuíveis, code models correspondentes e bibliotecas de componentes. O empacotamento verifica a instalação silenciosa em caminho com espaços, CLI, importação de `wx`/`pcbnew` e simulação analógica, digital e memória. O executável final fica em `dist/`, acompanhado de SHA-256.

## Compilação remota sem publicação

O workflow `release.yml` aceita apenas disparo manual em `main`, tem permissão `contents: read` e guarda o instalador como artefato temporário `windows-x86_64`. Não cria release, tag, commit de binários ou upload de assets para GitHub Releases. Após concluir, baixe o artefato para `dist/`:

```bash
gh run download ID_DA_EXECUCAO --name windows-x86_64 --dir dist
(cd dist && sha256sum --check CircuitAliveEDA-10.0.7-alpha.1-windows-x86_64-setup.exe.sha256)
```

A validação local Linux não comprova um build MSVC. Consulte as etapas da execução Windows antes de considerar o instalador validado.
