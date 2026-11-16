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
3. No campo **version**, informe o nome público desejado, por exemplo
   `10.0.7-alpha.4.1` ou `Preview de novembro`. Ele determina o título e a tag no GitHub.
4. Em **package_version**, informe a versão completa do aplicativo, por exemplo
   `10.0.7-alpha.4.1` ou `10.0.7-4.1`. Esse campo é independente do nome público.
5. Escolha **target** conforme a tabela abaixo.
6. Marque **publish_release** para publicar, ou desmarque para gerar apenas artefatos de teste.
7. Clique no botão verde **Run workflow**.

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

Essa única solicitação inicia todo o processo. O nome público aceita texto livre
de até 100 caracteres imprimíveis. Para nomes de arquivos e tags, espaços e
pontuação viram hífens, acentos latinos são removidos e apenas letras ASCII,
números, pontos, sublinhados e hífens são mantidos. O nome precisa conter pelo
menos uma letra ou número. Por exemplo, `October Preview 2026!` produz a tag
`vOctober-Preview-2026`, o título `CircuitAliveEDA October Preview 2026!` e
arquivos com nomes fixos, como `CircuitAliveEDA-windows-x86_64-setup.exe`.

Não pode haver uma tag/release anterior com a mesma tag normalizada; confira
possíveis colisões como `Test 1` e `Test-1`. A versão técnica vem sempre de
**package_version**: o nome público e o número da execução não a substituem.
A identificação completa segue `KiCad-CircuitAlive`: em `10.0.7-alpha.4.1`,
`10.0.7` identifica a base KiCad e `alpha.4.1` identifica o CircuitAlive.
Uma versão final pode usar `10.0.7-4.1`, mantendo as duas partes.
O aplicativo, o instalador e o **Sobre** preservam a identificação completa.
O CircuitAlive aceita de um a quatro componentes numéricos, cada um entre 0 e
65535, para permitir recursos de versão válidos no Windows. Os campos binários
numéricos do Windows usam somente a parte CircuitAlive preenchida com zeros
(`4.1.0.0`); os campos textuais mantêm `10.0.7-alpha.4.1`.
RPM usa `Version: 10.0.7` e `Release: 0.alpha.4.1.fc44` para alpha, ou
`Release: 1.4.1.fc44` para a versão final. DEB usa `10.0.7~alpha.4.1-1` e
`10.0.7+4.1-1`, respectivamente. As versões finais são ordenadas após as alpha.
Veja as regras de [versão do instalador Windows](https://jrsoftware.org/ishelp/topic_setup_versioninfoversion.htm),
[RPM](https://rpm.org/docs/latest/manual/spec.html) e
[Debian](https://www.debian.org/doc/debian-policy/ch-controlfields.html#version).
As releases são publicadas como **estáveis**, inclusive quando o nome contém
`alpha`. Não é preciso criar a tag ou cadastrar um token pessoal: o job de
publicação usa o `GITHUB_TOKEN` do próprio Actions com `contents: write`.

## Etapas da execução

EXE, RPM, DEB e AppImage incluem as bibliotecas completas e registram símbolos e
footprints automaticamente na primeira abertura. Veja [bibliotecas prontas na
primeira execução](BUNDLED_LIBRARIES.md), incluindo as verificações de caminhos e
de recuperação das configurações de pacotes anteriores.

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

Para a versão de exemplo, com `target=all`, a release `v10.0.7-alpha.3` terá:

- `CircuitAliveEDA-fedora44-x86_64.rpm`
- `CircuitAliveEDA-linux-x86_64.AppImage`
- `CircuitAliveEDA-ubuntu24.04-amd64.deb`
- `CircuitAliveEDA-windows-x86_64-setup.exe`
- `SHA256SUMS.txt` com os hashes de todos os instaladores publicados.

Os `.sha256` individuais continuam sendo gerados e conferidos durante o build,
mas não aparecem na lista de downloads da release. O `release-manifest.json`
também é gerado durante a validação, com a relação entre os nomes internos e
públicos, mas não é anexado à release. O nome é simplificado na publicação,
sem alterar o conteúdo binário dos instaladores. Para conferir um pacote baixado junto ao TXT:

```bash
sha256sum --ignore-missing -c SHA256SUMS.txt
```

Na lista **Artifacts** do Actions, cada instalador aparece separadamente com
seu nome **interno** (incluindo a distribuição/arquitetura e a versão técnica).
O download entrega o próprio `.rpm`, `.AppImage`, `.deb` ou `.exe`, sem um ZIP
adicional. Os artefatos
`CircuitAliveEDA-<versão>-linux-SHA256`, `-ubuntu24.04-SHA256` e
`-windows-SHA256` guardam os checksums em ZIP. O job de publicação reúne os
instaladores e esses checksums antes de validar e publicar os arquivos com nomes públicos simplificados.

Os artefatos terminados em `diagnostics` contêm logs de build, testes e
dependências; não são instaladores. `windows-runtime-x86_64` é a instalação
intermediária usada para reempacotar o EXE. Nas execuções antigas, anteriores
à separação por formato, `fedora-x86_64` contém RPM, AppImage e checksums;
`deb-x86_64` e `windows-x86_64` contêm seus instaladores e checksums em ZIP.
Uma execução já iniciada mantém os nomes e as actions do commit original.

O pacote para DNF é o **`.rpm`**; `.dnf` não é uma extensão de pacote.

```bash
sudo dnf install ./CircuitAliveEDA-fedora44-x86_64.rpm
```

No Ubuntu 24.04, instale o DEB com APT para resolver suas dependências:

```bash
sudo apt install ./CircuitAliveEDA-ubuntu24.04-amd64.deb
```

O `.deb` usa bibliotecas e Python nativos do Ubuntu 24.04. Não é uma conversão do
RPM do Fedora e não promete instalação em todas as versões de Debian/Ubuntu.
Internamente a versão Debian usa, por exemplo, `10.0.7~alpha.4.1-1`, para que uma versão estável
posterior seja considerada mais nova pelo gerenciador de pacotes.

Para executar a AppImage:

```bash
chmod +x CircuitAliveEDA-linux-x86_64.AppImage
./CircuitAliveEDA-linux-x86_64.AppImage
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

### Cache entre execuções

- **Windows:** o vcpkg continua validando o ABI das dependências. A aplicação usa
  sccache 0.16.0, instalado por uma revisão fixa da action da Mozilla. Cada objeto
  concluído é gravado no cache do Actions durante a compilação, inclusive antes
  de uma falha posterior. O cache considera compilador, argumentos e conteúdo
  dos fontes/cabeçalhos. Só no CI, `CA_USE_SCCACHE=1` configura os launchers C/C++,
  desativa PCH e usa informações de debug embutidas (`/Z7`), necessárias para
  armazenar objetos MSVC. As funcionalidades e os testes permanecem habilitados.
  A opção de avisos `-external:env:INCLUDE` usa o prefixo `-`, também aceito pelo
  MSVC: com `/`, o parser do sccache 0.16 interpreta essa opção como outro fonte
  e deixa de armazenar os objetos C++. Um teste nativo usa essa opção ao compilar
  C e C++ repetidamente, verifica uma mudança de cabeçalho e confere se o cache
  recusou a chamada antes do build completo. Estatísticas aparecem no post-step
  da action.
  Se o setup do cache estiver indisponível, o job compila pelo caminho normal;
  falha de comunicação com o servidor também pode recorrer ao compilador local.
  As bibliotecas da interface também propagam `/FIjson_common.h` aos seus
  consumidores MSVC. Isso faz os painéis importarem o JSON de `kicommon` antes
  de usar os helpers inline, evitando definições duplicadas na ligação da DLL.
  O teste inicial `qa_circuitalive_json_link` inclui esses helpers antes dos
  outros cabeçalhos e verifica a serialização dos controles. O worker isolado
  continua usando suas próprias instâncias de JSON.
- **Fedora e Ubuntu:** restauram o ccache e tentam salvar os objetos concluídos
  mesmo após uma falha ou cancelamento normal. Encerramento forçado do runner
  pode impedir esse salvamento. Os logs mostram estatísticas zeradas por execução;
  o conteúdo do compilador participa da identificação dos objetos.
- **Ubuntu:** ngspice e NNG instalados têm cache separado, salvo antes da aplicação.
  Esse prefixo inclui as licenças e a lista de autores usadas pelo DEB. O
  empacotamento não depende das pastas de fontes, ausentes após uma restauração.
  Caches antigos sem esses arquivos são rejeitados antes de compilar a aplicação;
  o novo perfil recompila essas duas dependências uma vez, preservando o ccache
  dos objetos da aplicação.
  A chave considera arquitetura, compiladores, versões dos pacotes instalados,
  versões fixadas e receita de compilação. Um manifesto confere hashes de todos
  os arquivos e destinos dos links; cache incompleto, alterado ou incompatível é
  descartado e reconstruído. Só instalações concluídas são salvas.
  Falhas de transferência dos caches não bloqueiam a compilação normal.

A primeira execução com os novos perfis aquece os caches; mudanças no ambiente
ou nas opções podem exigir nova compilação. Não se restaura uma árvore CMake
antiga nem se omitem testes/linkedição/validação dos pacotes. Os caches são uma
otimização: objetos compatíveis são reaproveitados e os demais são recompilados.

O script Ubuntu também pode executar as fases `tools`, `dependencies` e `build`
separadamente; sem argumento continua executando todas. `build` exige uma
instalação nativa validada para o perfil atual.

Referências: [sccache com CMake/MSVC](https://github.com/mozilla/sccache/blob/v0.16.0/README.md#usage)
e [backend de cache do Actions](https://github.com/mozilla/sccache/blob/v0.16.0/docs/GHA.md).

## Verificação local da configuração

As actions principais usam runtime Node.js 24: checkout v7, github-script v8,
cache v5, upload-artifact v7 e download-artifact v8. A action de setup do sccache
está fixada por commit e instala a versão indicada acima. O runner hospedado
fornece os runtimes necessários; não é necessário
instalar Node.js nos pacotes do CircuitAliveEDA.

```bash
actionlint
python3 qa/tests/cli/test_release_artifacts.py
python3 qa/tests/cli/test_deb_dependency_cache.py
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
