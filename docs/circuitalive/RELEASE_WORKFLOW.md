# Release manual: todos os pacotes ou um formato individual

O workflow **Release packages (all or individual)** (`.github/workflows/publish-release.yml`)
compila RPM, AppImage, DEB e EXE em uma única execução manual, ou somente os
formatos escolhidos. Pode publicar uma GitHub Release ou guardar apenas artefatos.
Pushes, commits e criação de tags não acionam esse workflow. A configuração precisa
estar na branch `main` do GitHub para que o botão apareça; alterações apenas locais
ainda não mudam os Actions do repositório.

## Como gerar

1. Abra **GitHub → Actions → Release packages (all or individual)**.
2. Clique em **Run workflow** e selecione a branch **main**.
3. Informe uma versão nova, por exemplo `10.0.7-alpha.2`, sem o prefixo `v`.
4. Escolha **target** conforme a tabela abaixo.
5. Marque **publish_release** para publicar, ou desmarque para gerar apenas artefatos de teste.
6. Clique no botão verde **Run workflow**.

| target | Pacotes gerados | Compilações executadas |
| --- | --- | --- |
| `all` | RPM, AppImage, DEB e EXE | Fedora, Ubuntu e Windows em paralelo |
| `linux` | RPM, AppImage e DEB | Fedora e Ubuntu em paralelo |
| `exe` | EXE | Somente Windows |
| `rpm` | RPM | Somente Fedora, sem gerar AppImage |
| `appimage` | AppImage | Somente Fedora, sem gerar RPM |
| `deb` | DEB | Somente Ubuntu 24.04 |

Para uma versão pronta para distribuição, use `all` com **publish_release**
marcado. Para testar uma correção de plataforma, escolha o formato e desmarque
**publish_release**. Assim, repetir um teste não cria outra release nem exige uma
versão inédita. A compilação dos programas continua completa na plataforma
selecionada; não se trata apenas de reempacotar um executável antigo.

Com **publish_release** marcado, cada solicitação cria uma release nova contendo
somente os formatos selecionados. Não acrescenta arquivos a uma release existente:
use uma versão ainda livre. Para distribuir todos juntos, use `all`.

Essa única solicitação inicia todo o processo. O perfil atual aceita versões
`X.Y.Z-alpha.N`, de acordo com a validação de versão existente no CMake e nos
empacotadores. As releases ficam marcadas como **Pre-release**, adequadas à fase
alpha. Não é preciso criar a tag ou cadastrar um token pessoal: o job de
publicação usa o `GITHUB_TOKEN` do próprio Actions com `contents: write`.

## Etapas da execução

| Job | O que faz |
| --- | --- |
| Validate version and release destination | Confere branch, versão e formatos. Se for publicar, recusa tag/release já existente. Fixa o commit de origem. |
| Windows installer | Compila com MSVC, executa os testes e salva o runtime completo; outro job gera o EXE com Inno Setup x64 e testa a instalação. |
| Fedora packages | Compila no contêiner Fedora 44 e gera os formatos Fedora selecionados. RPM e AppImage compartilham uma única compilação quando pedidos juntos. |
| Ubuntu DEB package | Compila nativamente no Ubuntu 24.04, incluindo ngspice 47 e nng 1.9.0 fixados. Gera o DEB com dependências calculadas pelo dpkg, extrai o pacote e testa CLI, Python/wx e o worker. |
| Publish selected verified packages together | Aguarda todos os builds selecionados, confere os formatos e SHA-256, envia os arquivos para um rascunho e então publica. É omitido no modo de artefatos. |

Os jobs selecionados executam em paralelo, conforme a disponibilidade dos
runners. Todos usam o mesmo commit escolhido ao iniciar a execução. O Windows
mantém o grupo de concorrência existente, com `cancel-in-progress: false`.
Uma compilação Windows em andamento pode fazer a nova aguardar sua vez.

## Arquivos disponíveis

Para a versão de exemplo, com `target=all`, a release `v10.0.7-alpha.2` terá:

- `CircuitAliveEDA-10.0.7-alpha.2-fedora44-x86_64.rpm`
- `CircuitAliveEDA-10.0.7-alpha.2-linux-x86_64.AppImage`
- `CircuitAliveEDA-10.0.7-alpha.2-ubuntu24.04-amd64.deb`
- `CircuitAliveEDA-10.0.7-alpha.2-windows-x86_64-setup.exe`
- Um arquivo `.sha256` para cada pacote e `release-manifest.json` com o commit e os hashes.

Na lista **Artifacts** do Actions, cada instalador também aparece separadamente
com esse nome e extensão. O download entrega o próprio `.rpm`, `.AppImage`,
`.deb` ou `.exe`, sem um ZIP adicional. Os artefatos
`CircuitAliveEDA-<versão>-linux-SHA256`, `-ubuntu24.04-SHA256` e
`-windows-SHA256` guardam os checksums em ZIP. O job de publicação reúne os
instaladores e esses checksums antes de validar e publicar os arquivos.

Os artefatos terminados em `diagnostics` contêm logs de build, testes e
dependências; não são instaladores. `windows-runtime-x86_64` é a instalação
intermediária usada para reempacotar o EXE. Nas execuções antigas, anteriores
à separação por formato, `fedora-x86_64` contém RPM, AppImage e checksums;
`deb-x86_64` e `windows-x86_64` contêm seus instaladores e checksums em ZIP.
Uma execução já iniciada mantém os nomes e as actions do commit original.

O pacote para DNF é o **`.rpm`**; `.dnf` não é uma extensão de pacote.

```bash
sudo dnf install ./CircuitAliveEDA-10.0.7-alpha.2-fedora44-x86_64.rpm
```

No Ubuntu 24.04, instale o DEB com APT para resolver suas dependências:

```bash
sudo apt install ./CircuitAliveEDA-10.0.7-alpha.2-ubuntu24.04-amd64.deb
```

O `.deb` usa bibliotecas e Python nativos do Ubuntu 24.04. Não é uma conversão do
RPM do Fedora e não promete instalação em todas as versões de Debian/Ubuntu.
Internamente a versão Debian usa `10.0.7~alpha.2-1`, para que uma versão estável
posterior seja considerada mais nova pelo gerenciador de pacotes.

Para executar a AppImage:

```bash
chmod +x CircuitAliveEDA-10.0.7-alpha.2-linux-x86_64.AppImage
./CircuitAliveEDA-10.0.7-alpha.2-linux-x86_64.AppImage
```

Os pacotes são x86_64. A AppImage preserva a base Linux atual: **Fedora 44 / glibc
2.43 ou ambiente mais novo compatível**. O formato AppImage, por si só, não torna
os binários compatíveis com distribuições mais antigas. OpenGL e drivers de vídeo
continuam dependendo do sistema. O instalador Windows usa MSVC v143.

## Se alguma etapa falhar

A release pública só é criada após todos os builds selecionados e as verificações
dos pacotes pedidos. Falha em qualquer um deles bloqueia a publicação; os jobs
omitidos por escolha de formato não a bloqueiam. Os arquivos já concluídos ficam
nos artefatos individuais com as extensões `.rpm`, `.AppImage`, `.deb` e/ou `.exe`; os logs de
diagnóstico também ficam no Actions. Se o upload da
release falhar, ela pode permanecer como rascunho para inspeção, sem publicação
parcial. Antes de iniciar tudo novamente com a mesma versão, confira esse rascunho
e a tag: o preparo recusa destinos já existentes para não sobrescrever releases.

O workflow **Local Windows installer (MSVC)** continua disponível para gerar
somente o `.exe` como artefato, sem publicar release. O novo workflow permite
essa mesma escolha com `target=exe` e **publish_release** desmarcado.

### Repetir somente o empacotamento Windows

O job de compilação agora preserva `windows-runtime-x86_64` por 14 dias, antes de
baixar as bibliotecas de componentes e gerar o instalador. Se apenas o job
**Package and verify Windows installer** falhar, use **Re-run failed jobs**
para repetir o empacotamento com o mesmo código, sem recompilar os aplicativos.

Se a correção do empacotador exigir um novo commit, abra **Local Windows installer
(MSVC) → Run workflow**, mantenha a mesma versão dos binários e informe em
**runtime_run_id** o número da execução que salvou o runtime. O workflow baixa e
verifica os hashes desse artefato e omite a compilação. A origem dos binários
continua registrada em `PACKAGE-INFO.json`; trocar a versão sem recompilar é
recusado. Esse modo gera somente artefatos, sem publicar uma release.

A execução `39308253615` é anterior a esse checkpoint: salvou apenas logs e
dependências, portanto seus aplicativos precisam ser recompilados uma vez.

O empacotador usa Inno Setup **7.1.0 x64**, fixado por SHA-256 e assinatura do
fornecedor. Substitui o NSIS, que falhou com `error mapping datablock` no pacote
completo. Mantém um único `.exe`, compressão LZMA2 com dois threads e verifica CLI,
Python/wx/pcbnew e worker a partir da instalação, sem caminhos do build no `PATH`.
Os logs, o script `.iss` e o inventário de tamanhos ficam no artefato
`windows-installer-diagnostics`. Um EXE único tem limite de 4,2 GB comprimidos no
Inno Setup; pacotes que ultrapassem isso exigirão outra forma de distribuição,
como partes externas. As bibliotecas 3D continuam incluídas.

## Verificação local da configuração

Todos os workflows e a action composta de checkpoint usam actions com runtime
Node.js 24: checkout v7, github-script v8, cache v5, upload-artifact v7 e
download-artifact v8. O runner hospedado fornece esse runtime; não é necessário
instalar Node.js nos pacotes do CircuitAliveEDA.

```bash
actionlint
python3 qa/tests/cli/test_release_artifacts.py
python3 qa/tests/cli/test_windows_installer.py
bash -n tools/circuitalive/release/build-linux.sh tools/circuitalive/release/build-deb.sh tools/circuitalive/dependencies-fedora.sh
```

O teste de publicação é offline e substitui os comandos externos: cobre pacotes
ausentes, corrompidos ou com formato incorreto, seleção individual e completa, e garante que falhas de upload não
publiquem o rascunho. Esses checks não substituem a primeira execução completa nos
runners. As ferramentas AppImage e as dependências compiladas no Ubuntu têm versões e hashes fixados em
`tools/circuitalive/release/linux-toolchain.json`.

Referências: [workflows reutilizáveis do GitHub](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows)
e [appimagetool](https://github.com/AppImage/appimagetool), além do
[empacotador dpkg-deb](https://manpages.debian.org/testing/dpkg/dpkg-deb.1.en.html),
[limites do NSIS](https://nsis.sourceforge.io/I_get_an_error_when_compiling_large_installers),
[Inno Setup x64](https://jrsoftware.org/ishelp/topic_64bit.htm) e
[limite do EXE único](https://jrsoftware.org/ishelp/topic_setup_diskspanning.htm).
