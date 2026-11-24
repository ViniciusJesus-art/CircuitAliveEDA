# Releases locais, pelo Actions ou combinadas

Os dois caminhos continuam disponíveis. Cada envio acrescenta os formatos
selecionados à mesma release, identificada pelo nome/versão informado. Você pode
enviar RPM/AppImage pelo Fedora e pedir o EXE ao Actions, ou fazer o contrário.
Os commits podem ser diferentes; alterações locais sem commit entram no build
local. Não é necessário reunir todos os instaladores em uma máquina.

## Pelo GitHub Actions

Depois de enviar estes workflows à `main`, abra **Actions → Release packages
(all or individual) → Run workflow**:

1. Selecione `main`.
2. Informe `version`, por exemplo `10.0.7-alpha.4.2.0`.
3. Escolha `target`: `all`, `windows`/`exe`, `fedora`, `linux`, `rpm`, `appimage` ou `deb`.
4. Marque `publish_release` para criar ou completar essa release. Desmarque para
   gerar somente os arquivos para download nos artefatos da execução.

`all` gera EXE, RPM, AppImage e DEB. `fedora` gera RPM e AppImage; `linux` inclui
também DEB. Cada plataforma usa seu build nativo: Windows 2022/MSVC, Fedora 44
em contêiner e Ubuntu 24.04. Os caches de compilação e de dependências permanecem.
Só é executada a plataforma necessária para o alvo escolhido.

O campo **version** alimenta o nome da release, os arquivos, os cabeçalhos de
versão do programa, a janela Sobre e o instalador. Não há um segundo campo de
versão que possa divergir. Use a mesma versão ao completar uma release existente;
o envio preserva sua descrição, tag e os instaladores das outras plataformas.

Exemplo: envie `10.0.7-alpha.4.2.0` pelo Fedora local. No Actions, informe essa
mesma versão, escolha `windows` e marque `publish_release`. O EXE será adicionado
à release já existente. Também funciona começando pelo Windows no Actions e
completando pelo Fedora local.

O workflow **Windows installer (MSVC)** também está disponível separadamente
para compilar/testar e guardar artefatos. Seu campo `runtime_run_id` permite
reempacotar um runtime preservado sem recompilar C++. Os workflows de auditoria,
preparo de dependências e validação Windows também foram restaurados.

Pushes e tags não iniciam releases automaticamente. O Actions usa o código já
enviado ao GitHub; o comando local usa os arquivos presentes na máquina.

## Comando local

Instale Python **3.11+**, GitHub CLI e as ferramentas de compilação da plataforma.
Autentique uma vez com `gh auth login`. No Fedora, instale `gh` com
`sudo dnf install gh` e tenha Docker ou Podman funcional. No Windows, use MSVC
2022 e PowerShell 7; veja [BUILD_WINDOWS.md](BUILD_WINDOWS.md).

Na raiz do checkout Fedora:

```bash
python3 tools/circuitalive/release/release.py 10.0.7-alpha.4.2.0
```

Na raiz do checkout Windows:

```powershell
python tools/circuitalive/release/release.py 10.0.7-alpha.4.2.0
```

O comando compila, testa, empacota e envia os formatos daquela plataforma. No
Fedora gera RPM/AppImage usando o contêiner Fedora 44; no Windows gera EXE.
A primeira máquina cria e publica a release depois do upload; a seguinte a completa.

Se o build terminou e somente o envio falhou, acrescente `--upload-only` para
reutilizar `dist/`. Os hashes e a versão do registro local são verificados; o
commit atual pode ser diferente daquele usado no build. Execute sem essa opção
para recompilar e substituir somente os instaladores da plataforma atual.

Use `--engine docker` ou `--engine podman` para escolher o contêiner Linux;
`--repository dono/repositorio` muda o destino. O padrão é
`ViniciusJesus-art/CircuitAliveEDA`.

## Arquivos públicos

Para a versão de exemplo, a release completa contém:

- `CircuitAliveEDA-10.0.7-alpha.4.2.0.exe`
- `CircuitAliveEDA-10.0.7-alpha.4.2.0.rpm`
- `CircuitAliveEDA-10.0.7-alpha.4.2.0.AppImage`
- `CircuitAliveEDA-10.0.7-alpha.4.2.0.deb`
- `CircuitAliveEDA-10.0.7-alpha.4.2.0-auxiliares.zip`

O ZIP reúne `SHA256SUMS.txt`, o manifesto geral e os metadados/checksums de cada
formato em sua própria pasta. Ao acrescentar uma plataforma, o arquivo anterior
é baixado e atualizado, preservando os registros das demais. Não há checksums
ou manifestos avulsos na lista principal. Logs de diagnóstico e checkpoints de
runtime ficam nos artefatos da execução do Actions, separados dos downloads da release.

Os instaladores gerados localmente também usam esses nomes em `dist/`. Checksum
local só é criado após as validações do empacotador. O envio aceita ainda os
nomes internos antigos, para reaproveitar builds já concluídos, e publica com
os nomes acima.

A versão técnica segue `KiCad-CircuitAlive`: `10.0.7-alpha.4.2.0` ou `10.0.7-4.2.0`,
com um a quatro números CircuitAlive entre 0 e 65535. Bibliotecas, traduções e
runtimes continuam incluídos. A AppImage exige Fedora 44 / glibc 2.43 ou runtime
compatível mais novo; o DEB usa as dependências nativas do Ubuntu 24.04.

## Validação e uso avançado

`publish-release.py` continua disponível para conferir um diretório de pacotes
ou enviar uma seleção, usando o mesmo publicador incremental. `--formats all`
exige os quatro formatos; uma seleção como `exe` ou `rpm,appimage` exige somente
os arquivos correspondentes. O SHA informado é metadado do build, não uma
exigência de igualdade com os outros pacotes da release.

O Actions envia os artefatos já compilados usando `release.py --from-artifacts
--formats ...`; essa opção verifica os arquivos/checksums sem recompilar e sem
exigir o registro de um build local. O token do job de publicação tem permissão
`contents: write`. Releases imutáveis configuradas no GitHub não aceitam acréscimos.

Os testes do módulo estão em `test_release_artifacts.py`, `test_incremental_release.py`,
`test_release_workflow.py`, `test_release_versions.py` e `test_windows_installer.py`,
em `qa/tests/cli/`. Eles não substituem os builds e testes nativos executados
pelos empacotadores de cada plataforma.
