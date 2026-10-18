# Auditoria de referências ao KiCad — CircuitAliveEDA

Data: 18/10/2026. Escopo: verificação estática; nenhuma alteração funcional ou de identidade foi aplicada.

## Resultado

**O KiCad ainda aparece em muitos pontos fora do “Sobre”. A identidade CircuitAliveEDA já está parcialmente aplicada, mas a interface, os recursos de distribuição e os documentos ainda misturam as duas identidades.**

É possível concentrar os créditos visíveis ao projeto original no “Sobre”. Isso exige revisar textos, links e metadados, sem substituir indiscriminadamente identificadores de formatos, APIs, configurações ou atribuições existentes no código.

## Método e abrangência

- Listagem com `git ls-files -c -o --exclude-standard`, procurando `kicad` sem distinguir maiúsculas/minúsculas no conteúdo textual e nos nomes desses arquivos.
- 18.847 caminhos listados; 13.433 arquivos textuais lidos; 5.413 binários desconsiderados na busca de conteúdo; um caminho não regular/indisponível.
- 7.440 arquivos textuais com referências no conteúdo, totalizando 116.813 linhas e 136.569 ocorrências. São referências de todos os tipos, inclusive comentários, traduções, testes, exemplos e identificadores; **não são 136.569 problemas de interface**.
- 7.702 arquivos textuais com referência no nome ou conteúdo, indexados em [KICAD_REFERENCE_FILES.tsv](KICAD_REFERENCE_FILES.tsv).
- 514 linhas candidatas de strings, URLs e propriedades de interface em módulos de aplicação estão em [KICAD_UI_CANDIDATES.tsv](KICAD_UI_CANDIDATES.tsv), com arquivo e linha. A seleção é heurística: contém identificadores técnicos e não captura necessariamente todas as strings multilinha. Não é uma lista de substituições aprovadas.
- A contagem corresponde ao estado anterior à criação destes relatórios. Não inclui conteúdo binário, histórico Git, arquivos ignorados, dependências instaladas ou varredura integral das árvores geradas `build/` e `.circuitalive/`. O cache da compilação local foi consultado pontualmente.
- Não houve execução gráfica, inspeção visual de todos os ícones, compilação ou testes de comportamento. Portanto, os achados confirmam presença no código e nos recursos; a exibição pode depender da plataforma, da configuração e de sobrescritas em execução.

## Referências visíveis e comportamentais confirmadas

As linhas abaixo apontam para o código-fonte no momento da auditoria.

| Área | Evidência | Consequência / tratamento sugerido |
| --- | --- | --- |
| Menu do gerenciador | `kicad/menubar.cpp:124`: `Import Non-KiCad Project...` | Rever o rótulo de importação. |
| Menu do esquemático | `eeschema/menubar.cpp:104` e `eeschema/tools/sch_actions.cpp:1218`: importação de esquemático “Non-KiCad” | Rever o texto mantendo clara a distinção entre formatos. |
| Editor PCB | `pcbnew/tools/pcb_actions.cpp:1227`: `Import Non-KiCad Board File...` | Mesmo caso no editor PCB. |
| Ajuda e primeiros passos | `common/tool/actions.cpp:1391`; `common/tool/common_control.cpp:54`, `:56`, `:329`, `:349` | Tooltip cita KiCad; documentação e mensagens de ajuda ainda remetem ao projeto original. Trocar apenas o nome não muda o destino. |
| Contribuição e doação | `common/tool/actions.cpp:1421`, `:1428`; `common/tool/common_control.cpp:54`, `:55`, `:387`, `:402` | Ações encaminham para contribuição/doação ao KiCad. Se preservadas como apoio ao projeto original, concentrar no “Sobre” e deixar o destinatário explícito. |
| Preferências gerais | `common/dialogs/panel_common_settings_base.cpp:41`, `:198`, `:241`, `:453`; `common/dialogs/panel_common_settings.cpp:96` | Textos sobre gráficos, cursores, tamanho dos ícones e diretório de dados usam KiCad. Há propriedades correspondentes no `.fbp`. |
| Plugins / API | `common/dialogs/panel_plugin_settings_base.cpp:17`, `:27`, `:31`; `common/dialogs/panel_plugin_settings.cpp:121`; `kicad/pcm/pcm.cpp:1265` | “KiCad API” e mensagens de conexão aparecem nas preferências e instalação de plugins. Rever rótulos preservando o protocolo compatível. |
| Atualização do aplicativo | `kicad/update_manager.cpp:81`; `kicad/dialogs/dialog_update_notice.cpp:38`; `common/dialogs/panel_packages_and_updates_base.cpp:30` | Endpoint `https://downloads.kicad.org/api/v1/update`, aviso de nova versão e opção de procurar atualizações do KiCad. Ver ressalva abaixo. |
| Mensagens comuns | `common/pgm_base.cpp:683`, `:724`; `kicad/project_tree_pane.cpp:1127`; `gerbview/tools/gerbview_inspection_tool.cpp:183` | Erros de idioma e editor de texto selecionado ainda identificam o aplicativo como KiCad. |
| Compatibilidade / importação | `eeschema/files-io.cpp:267`, `:668`; `pcbnew/files.cpp:193`, `:859`; `pagelayout_editor/files.cpp:271` | Avisos de versões e formatos citam KiCad. Distinguir o nome do aplicativo do nome do formato. |
| Fontes e camadas | `pcbnew/dialogs/dialog_text_properties_base.cpp:120`; `eeschema/dialogs/dialog_text_properties_base.cpp:93`; `pcbnew/dialogs/dialog_imported_layers_base.cpp:36`, `:99`; `pcbnew/dialogs/dialog_plot_base.cpp:356` | “KiCad Font”, “KiCad Layers” e outros rótulos continuam visíveis. Rever a apresentação sem alterar os valores persistidos. |
| Seletores de arquivos | `common/wildcards_and_files_ext.cpp:242`, `:249`, `:269`, `:358` | Filtros exibem “KiCad ... files”. Podem ser reformulados; extensões e detecção de formato devem ser preservadas. |
| Histórico local | `common/local_history.cpp:902`, `:988`, `:1003`, `:1099` | Arquivos explicativos gerados e autoria dos commits locais ainda usam KiCad. |
| Template inicial | `kicad/project_template.cpp:482` | HTML do template padrão contém “Default KiCad project template.” |
| Exportações | `common/plotters/PDF_plotter.cpp:2008`; `common/plotters/GERBER_plotter.cpp:290`; `pcbnew/exporters/place_file_exporter.cpp:235`, `:314`; `pcbnew/exporters/gerber_jobfile_writer.cpp:154`; `pcbnew/board_statistics_report.cpp:726`; `eeschema/netlist_exporters/netlist_exporter_spice.cpp:126` | PDFs, Gerbers, relatórios, arquivos de posicionamento e netlists podem identificar o gerador como KiCad. Revisar os campos de identificação separadamente dos tokens de formato. |
| Traduções | `translation/pofiles/pt_BR.po:299`, `:1811`, `:2216`, `:2330` e demais catálogos | Catálogos guardam nomes e mensagens do KiCad. A atualização deve partir das strings-fonte, regenerando/sincronizando os catálogos; entradas antigas podem não estar mais em uso. |

### Atualizador: estado real da compilação local

O endpoint original existe, mas **não foi constatada uma consulta ativa nesta compilação**:

- `CMakePresets.json:28` define `KICAD_UPDATE_CHECK=false`.
- `build/circuitalive/CMakeCache.txt:635` confirma `KICAD_UPDATE_CHECK:BOOL=FALSE`.
- `kicad/kicad_manager_frame.cpp:1415` condiciona a chamada automática a essa opção.
- `common/dialogs/panel_packages_and_updates.cpp:40` oculta a opção nas preferências quando o recurso está desativado.
- O padrão geral em `CMakeLists.txt:135` continua `ON`; uma compilação fora dos presets pode reativar esse comportamento.
- A atualização de pacotes/plugins tem um fluxo separado, visível em `kicad/kicad_manager_frame.cpp:1405`, e não deve ser confundida com a atualização do aplicativo.

## Integração com o sistema e distribuição

| Área | Evidência | Situação |
| --- | --- | --- |
| Atalhos Linux | `resources/linux/launchers/org.kicad.kicad.desktop.in:11` e demais launchers | Os nomes principais já usam CircuitAliveEDA. Descrições dos editores e conversor ainda citam KiCad (`resources/linux/launchers/org.kicad.eeschema.desktop.in:12`, `resources/linux/launchers/org.kicad.pcbnew.desktop.in:13`, `resources/linux/launchers/org.kicad.bitmap2component.desktop.in:12`). |
| Loja de aplicativos Linux | `resources/linux/metainfo/org.kicad.kicad.metainfo.xml.in:51`, `:57`, `:74`, `:82`, `:84` | Descrição cita o original; legendas e URLs das capturas são do KiCad; há links de ajuda, doação, tradução e contribuição, contato `devlist@kicad.org` e desenvolvedor “The KiCad Developers”. |
| Tipos de arquivo Linux | `resources/linux/mime/kicad-kicad.xml.in:5`, `:12`, `:27`, `:41`, `:55`, `:69` | Descrições exibidas pelo sistema usam KiCad; MIME types e assinaturas também incluem `kicad` por compatibilidade. |
| Identidade de instalação | `cmake/KiCadAppNames.cmake:12`; `include/circuitalive/branding.h:10` | O CMake ainda parte de `org.kicad`, enquanto a identidade de execução declara `org.circuitaliveeda.CircuitAliveEDA`. Rever o alinhamento dos IDs e dos arquivos instalados. |
| Windows | `resources/msw/kiwin32.rc:89`, `resources/msw/kiwin32-dll.rc:33`, `resources/msw/kicad.rc:18` e demais `.rc` | Propriedades dos executáveis/DLLs continuam com “KiCad EDA” e descrições dos editores. Avisos de copyright são atribuições, com tratamento distinto do nome comercial. |
| macOS | `resources/macos/plist/kicad.Info.plist.in:34`; `resources/macos/plist/eeschema.Info.plist.in:15` | Bundle principal ainda se chama KiCad; documentos e identificadores dos bundles também citam o original. |
| README e documentação | `README.md:18`, `:26`, `:143`, `:210`, `:238`; documentos em `docs/` e `doxygen/` | A origem é mencionada fora do “Sobre”, inclusive na apresentação e nos créditos/licenças. O README já possuía uma alteração local e não foi modificado por esta auditoria. |

## O que já está adaptado

- `include/circuitalive/branding.h:9`: nome centralizado como CircuitAliveEDA.
- `kicad/kicad.cpp:98` e `kicad/kicad_manager_frame.cpp:259`: identidade/título do gerenciador usam a marca própria.
- `common/tool/actions.cpp:1405`: “About CircuitAliveEDA”.
- `common/tool/actions.cpp:1435`: relato de problema identifica CircuitAliveEDA.
- `common/dialog_about/AboutDialog_main.cpp:116`: apresentação do derivado e créditos à base KiCad já estão no “Sobre”; links do projeto e de issues apontam para o repositório CircuitAliveEDA.
- `common/dialog_about/dialog_about.cpp:104`: tooltips de cópia de versão e relato de problema são sobrescritos com CircuitAliveEDA. Os textos KiCad ainda existentes no `.fbp`/`_base.cpp` para esses dois botões são resíduos da definição gerada, não prova de que aparecem em execução. O botão de doação continua ligado à ação de doação original.
- `CMakePresets.json:22` e `tools/circuitalive/runtime-env.sh:3`: configurações locais usam diretórios próprios, embora preservem nomes de variáveis compatíveis com KiCad.

## Critérios para uma futura correção

1. Usar CircuitAliveEDA nos nomes do produto, títulos, menus, mensagens, descrições de instalação e identificação do gerador quando o campo permitir.
2. Concentrar a apresentação do projeto original e seus links de crédito no “Sobre”. Rever também os blocos de comunidade e equipe para distinguir os colaboradores do original dos responsáveis pelo derivado.
3. Preservar os avisos de autoria e licenças existentes nos fontes, `AUTHORS.txt`, `LICENSE*` e dependências. A limpeza da marca na interface não é razão para apagar essas atribuições.
4. Preservar inicialmente extensões `.kicad_*`, tokens `kicad_sch`/`kicad_pcb`, tipos de biblioteca `KiCad`, contratos da API, nomes de variáveis `KICAD*`, nomes internos de ações e caminhos de bibliotecas utilizados pelo runtime. Alterá-los requer uma migração de compatibilidade própria.
5. Editar o `.fbp` e seus arquivos gerados em conjunto; atualizar os catálogos de tradução para evitar divergência entre idiomas.
6. Validar os fluxos no gerenciador e editores, importação/exportação, preferências, ajuda, instalação e “Sobre”. Para Windows/macOS, validar os pacotes específicos; apenas examinar seus templates não comprova o resultado instalado.

Não foram feitas substituições nesta etapa: o pedido foi de varredura e verificação. O inventário registra as referências e os pontos que exigem decisão técnica antes da mudança.
