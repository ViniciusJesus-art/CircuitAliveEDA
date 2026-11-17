# Desenvolvimento no Fedora e no Windows

Os dois ambientes usam este mesmo repositório e a branch `main`. Correções de
simulação, modelos e interface são compartilhadas. Diferenças de APIs, caminhos,
carregamento de DLLs e empacotamento ficam condicionadas à plataforma no CMake,
nos scripts ou no C++ (`_WIN32`/`__WXMSW__`), quando necessário.

Não copie a árvore de build de um sistema para o outro. O Fedora usa
`build/circuitalive`; o Windows usa `build/windows-msvc` e `stage/windows-msvc`.
Dependências, caches e binários locais continuam ignorados pelo Git.

## Sincronização

Antes de começar em cada máquina:

```text
git pull --ff-only origin main
```

Finalize e envie as alterações de uma máquina antes de continuar na outra.
Não execute `git pull` sobre alterações locais pendentes: faça commit primeiro.
Para distribuir uma versão, use o mesmo SHA completo e a mesma versão técnica
nas três compilações (Fedora local, Windows local e Ubuntu Actions).

## Windows nativo

Use Visual Studio 2022 / Build Tools 2022 com C++, SDK Windows e CMake/Ninja,
PowerShell 7 e Git. A dependência Python usada na compilação é a do manifesto
vcpkg, independente do Python instalado para executar verificações offline.
O primeiro preparo compila muitas bibliotecas; reserve tempo e espaço em disco.

Na raiz, em PowerShell 7:

```powershell
./tools/circuitalive/dev-windows.ps1 -Phase doctor
./tools/circuitalive/dev-windows.ps1 -Phase prepare -Version 10.0.7-alpha.4.1
./tools/circuitalive/dev-windows.ps1 -Phase build -Version 10.0.7-alpha.4.1
./tools/circuitalive/dev-windows.ps1 -Phase libraries
./tools/circuitalive/dev-windows.ps1 -Phase run
# Abrir diretamente um editor para medir seu carregamento:
./tools/circuitalive/dev-windows.ps1 -Phase run -Application eeschema
# Depois de testar o aplicativo, gerar e validar o instalador:
./tools/circuitalive/dev-windows.ps1 -Phase package -Version 10.0.7-alpha.4.1
```

`prepare` executa setup, dependências e configuração. `build` executa as validações
do simulador, compila a aplicação e prepara o runtime local. Após mudar C++,
repita `build` e `run`; repita `prepare` quando mudar dependências ou CMake.
`libraries` baixa e prepara também os modelos 3D, necessários ao instalador.
`run` usa as bibliotecas locais e inicia os executáveis de `stage/windows-msvc`.
Feche as instâncias abertas antes de recompilar ou atualizar o runtime.
Mantenha `-Version` igual entre prepare/build/package; a versão padrão é apenas
uma conveniência, não uma identificação automática de release.

Veja [BUILD_WINDOWS.md](BUILD_WINDOWS.md) para as fases individuais e o toolchain.

## Fedora local e Ubuntu no Actions

O fluxo interativo Fedora continua em [BUILD_FEDORA.md](BUILD_FEDORA.md).
Para RPM/AppImage, use o contêiner local documentado em
[RELEASE_WORKFLOW.md](RELEASE_WORKFLOW.md). Os Actions compilam apenas o DEB
Ubuntu. Os scripts de Fedora e Windows continuam versionados para uso local.

## Verificar desempenho e compatibilidade

Use a mesma revisão, projeto e bibliotecas nos dois sistemas. Registre o tempo
da primeira abertura e das aberturas seguintes do gerenciador, esquemático,
PCB e seletores de componentes. Compare o build Windows `Release` com o pacote
instalado: diferenças entre esses dois casos ajudam a localizar problemas de
empacotamento, descoberta de bibliotecas ou carregamento de DLLs.

As medições devem acompanhar as correções. Há um diagnóstico parcial da busca
no Windows, com validação do ajuste no executável ainda pendente. Execute as
validações nativas do simulador em ambas as plataformas antes de reunir os
pacotes de uma release.

Veja a [medição da interface Windows](WINDOWS_UI_PERFORMANCE.md) para a reprodução
de abertura/pesquisa, o gargalo identificado e a validação ainda pendente do
ajuste no executável.
