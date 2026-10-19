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
| Windows installer | Reutiliza o workflow MSVC existente: dependências, testes, compilação completa, NSIS e teste da instalação. |
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
nos artefatos `fedora-x86_64`, `deb-x86_64` e/ou `windows-x86_64`; os logs de
diagnóstico também ficam no Actions. Se o upload da
release falhar, ela pode permanecer como rascunho para inspeção, sem publicação
parcial. Antes de iniciar tudo novamente com a mesma versão, confira esse rascunho
e a tag: o preparo recusa destinos já existentes para não sobrescrever releases.

O workflow **Local Windows installer (MSVC)** continua disponível para gerar
somente o `.exe` como artefato, sem publicar release. O novo workflow permite
essa mesma escolha com `target=exe` e **publish_release** desmarcado.

## Verificação local da configuração

```bash
actionlint
python3 qa/tests/cli/test_release_artifacts.py
bash -n tools/circuitalive/release/build-linux.sh tools/circuitalive/release/build-deb.sh tools/circuitalive/dependencies-fedora.sh
```

O teste de publicação é offline e substitui os comandos externos: cobre pacotes
ausentes, corrompidos ou com formato incorreto, seleção individual e completa, e garante que falhas de upload não
publiquem o rascunho. Esses checks não substituem a primeira execução completa nos
runners. As ferramentas AppImage e as dependências compiladas no Ubuntu têm versões e hashes fixados em
`tools/circuitalive/release/linux-toolchain.json`.

Referências: [workflows reutilizáveis do GitHub](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows)
e [appimagetool](https://github.com/AppImage/appimagetool), além do
[empacotador dpkg-deb](https://manpages.debian.org/testing/dpkg/dpkg-deb.1.en.html).
