# Releases: enviar de cada máquina

## Fluxo simples recomendado

Cada máquina compila, testa, empacota e envia seus próprios instaladores para
**a mesma release**, sem copiar arquivos entre computadores. O primeiro envio
cria e publica a release; os seguintes acrescentam os pacotes daquela plataforma.
A ordem é livre: Windows ou Fedora pode enviar primeiro.

Nas duas máquinas, informe o mesmo nome/versão da release. Os commits podem
ser diferentes, inclusive por mudanças no README. Alterações locais ainda sem
commit também entram no build; não é necessário fazer commit ou push para enviar
os instaladores. Instale Python **3.11+** e GitHub CLI e autentique uma vez com
`gh auth login`. As ferramentas de compilação continuam sendo as descritas nos
guias Windows/Fedora; no Linux é necessário Docker ou Podman funcional, e no
Windows, PowerShell 7 e MSVC 2022.

No Fedora, na raiz do projeto:

```bash
python3 tools/circuitalive/release/release.py 10.0.7-alpha.4.2
```

No Windows, na raiz do projeto:

```powershell
python tools/circuitalive/release/release.py 10.0.7-alpha.4.2
```

A versão acima é um exemplo: use o mesmo valor para reunir os pacotes na mesma release.
O comando Fedora gera RPM e AppImage usando o contêiner Fedora 44. O comando
Windows gera o instalador EXE. A release já fica disponível após o primeiro envio,
e recebe os outros instaladores quando a outra máquina terminar. Os comandos
não fazem commit nem push automaticamente.

Se o build terminou, mas a rede ou o upload falhou, repita o comando adicionando
`--upload-only`: ele usa os pacotes já gerados. Um registro local confere a versão
e os hashes antes de permitir esse reenvio, mesmo se o checkout já estiver em
outro commit. O commit base fica registrado localmente como informação do
build, que pode incluir alterações locais ainda sem commit. Execute o comando normal para recompilar: ele substitui somente os pacotes
dessa plataforma na release informada.

O envio identifica a release pelo nome/versão informado, sem comparar commits
ou o destino da tag, e confere formato e checksum de cada instalador. Cada pacote recebe seu próprio `.sha256`, sem sobrescrever
os checksums do outro sistema. O upload inicial fica como rascunho até terminar;
se houver falha, o comando pode ser repetido. Releases antigas com a mesma tag
também podem receber os pacotes, sem exigir metadados especiais na descrição.
A tag existente é preservada; para uma release nova, o GitHub usa a branch padrão.
Releases configuradas como imutáveis no GitHub não permitem acréscimos.

Use `--engine docker` ou `--engine podman` para escolher o contêiner Linux;
`--repository dono/repositorio` muda o destino. O padrão é
`ViniciusJesus-art/CircuitAliveEDA`. Nenhuma publicação é disparada apenas por push.

O Ubuntu continua opcional pelo Actions, conforme descrito abaixo. O envio
independente Fedora/Windows não exige gerar ou baixar o DEB.

## Fluxo manual e Ubuntu no Actions


O mesmo repositório e a branch `main` atendem às duas máquinas de desenvolvimento.
A divisão de compilação é:

| Ambiente | Pacotes | Execução |
| --- | --- | --- |
| Fedora 44 x86_64 | RPM e AppImage | Local, usando os scripts existentes |
| Windows x64 / MSVC v143 | Instalador EXE | Local, com testes e execução nativos |
| Ubuntu 24.04 amd64 | DEB | GitHub Actions, disparo manual |

Os workflows Windows foram removidos; os scripts e testes Windows continuam no
repositório. Pushes e tags não iniciam compilações ou publicação. Alterações
locais só alteram os Actions depois de enviadas à `main` do GitHub.

## Fluxo manual antigo: uma versão e um commit para todos

A exigência de commit comum abaixo pertence apenas ao empacotamento/publicação
manual com `publish-release.py`. O fluxo simples com `release.py` agrupa pelo
nome/versão e permite commits diferentes.

Finalize, valide, faça commit e sincronize as mudanças entre as máquinas antes
de gerar os pacotes. Use o mesmo SHA completo e a mesma versão técnica nos três
ambientes. `git rev-parse HEAD` mostra o SHA. Não distribua builds com alterações
locais pendentes como se fossem a revisão publicada.

A versão tem o formato `KiCad-CircuitAlive`, por exemplo `10.0.7-alpha.4.1` ou
`10.0.7-4.1`. A parte CircuitAlive tem de um a quatro números entre 0 e 65535.
O nome público da release é independente dessa versão técnica.

## Ubuntu pelo GitHub Actions

Abra **Actions → Ubuntu DEB package → Run workflow**, selecione `main` e informe
`version` (nome público) e `package_version` (versão técnica). Deixe
`publish_release` desmarcado para reunir os quatro formatos depois. O workflow
salva o DEB e o SHA-256 como artefatos, com os testes e caches nativos existentes.
Não compila Fedora nem Windows. Se marcar publicação, cria uma release contendo
somente o DEB; não use essa opção quando quiser os quatro pacotes juntos.

Baixe o instalador e extraia o artefato de checksum para a pasta `dist/` usada
para reunir os pacotes. Não misture artefatos de versões ou commits diferentes.
O arquivo DEB deve manter seu nome interno, incluindo a versão técnica.

## Windows local

Em PowerShell 7, na raiz:

```powershell
./tools/circuitalive/dev-windows.ps1 -Phase prepare -Version 10.0.7-alpha.4.1
./tools/circuitalive/dev-windows.ps1 -Phase build -Version 10.0.7-alpha.4.1
./tools/circuitalive/dev-windows.ps1 -Phase libraries
./tools/circuitalive/dev-windows.ps1 -Phase run
./tools/circuitalive/dev-windows.ps1 -Phase package -Version 10.0.7-alpha.4.1
```

A geração do instalador testa o runtime instalado e só grava o `.sha256` após
sucesso. O EXE e seu checksum ficam em `dist/`. Se apenas o empacotamento falhar,
repita `package` com a mesma versão; não é necessário recompilar a aplicação.
Veja [BUILD_WINDOWS.md](BUILD_WINDOWS.md) e
[DEVELOPMENT_PLATFORMS.md](DEVELOPMENT_PLATFORMS.md).

## Fedora local: RPM e AppImage

O script de release exige Fedora 44, x86_64 e execução como root em contêiner.
No checkout Fedora, execute com Docker instalado (ou o equivalente com Podman):

```bash
export RELEASE_VERSION=10.0.7-alpha.4.1
export RELEASE_COMMIT=$(git rev-parse HEAD)
export RELEASE_FORMATS=rpm,appimage
docker run --rm \
  -e RELEASE_VERSION -e RELEASE_COMMIT -e RELEASE_FORMATS -e CIRCUITALIVE_JOBS=2 \
  -v "$PWD:/work" -w /work \
  fedora:44 bash tools/circuitalive/release/build-linux.sh
```

Em hosts com SELinux, use um volume com rotulagem apropriada ao contêiner
(por exemplo `:Z` para um checkout dedicado). O contêiner instala dependências,
compila, executa os testes e gera os dois pacotes e checksums em `dist/`.
`RELEASE_FORMATS=rpm` ou `appimage` gera só o formato escolhido. O desenvolvimento
Fedora fora do contêiner continua em [BUILD_FEDORA.md](BUILD_FEDORA.md).

## Reunir e publicar depois dos testes

Copie para uma única `dist/` os quatro instaladores e seus quatro `.sha256`
originais. Para `10.0.7-alpha.4.1`, os instaladores são:

- `CircuitAliveEDA-10.0.7-alpha.4.1-fedora44-x86_64.rpm`
- `CircuitAliveEDA-10.0.7-alpha.4.1-linux-x86_64.AppImage`
- `CircuitAliveEDA-10.0.7-alpha.4.1-ubuntu24.04-amd64.deb`
- `CircuitAliveEDA-10.0.7-alpha.4.1-windows-x86_64-setup.exe`

Em uma máquina com Python 3.11+ e GitHub CLI autenticado, confira os pacotes antes
sem publicar. Substitua `SHA_COMPLETO` pela revisão comum dos builds:

```text
python tools/circuitalive/release/publish-release.py --directory dist --version 10.0.7-alpha.4.1 --commit SHA_COMPLETO --formats all
```

O verificador exige todos os formatos, confere cabeçalhos e checksums e prepara
nomes públicos estáveis e `SHA256SUMS.txt`. O SHA fornecido é uma declaração do
operador: essa checagem não demonstra, sozinha, a origem interna de cada binário.
Confira os registros de build e `PACKAGE-INFO.json` antes da publicação.

Depois de testar as plataformas e revisar os arquivos, publique:

```text
python tools/circuitalive/release/publish-release.py --directory dist --version 10.0.7-alpha.4.1 --commit SHA_COMPLETO --formats all --repository ViniciusJesus-art/CircuitAliveEDA --publish
```

O script cria um rascunho, envia todos os arquivos e só então publica. Uma falha
no upload mantém o rascunho para inspeção. Use uma tag inédita. `--formats deb`,
`exe`, `rpm`, `appimage` ou uma lista separada por vírgulas permite publicar uma
seleção. `all` continua significando os quatro formatos no script local.

Os downloads públicos usam nomes sem a versão; ela permanece nos metadados.
A release contém também `SHA256SUMS.txt`. Bibliotecas, traduções e runtimes
continuam incluídos. A AppImage exige Fedora 44 / glibc 2.43 ou ambiente
compatível mais novo; o DEB usa as dependências nativas do Ubuntu 24.04.

## Verificações

```text
python qa/tests/cli/test_release_artifacts.py
python qa/tests/cli/test_release_versions.py
python qa/tests/cli/test_component_libraries.py
python qa/tests/cli/test_windows_installer.py
```

Execute também as validações nativas e teste o aplicativo em cada plataforma.
Os checks offline não substituem compilação, testes GUI nem medição de desempenho.
