# CircuitAliveEDA

Fork do KiCad para desenvolver simulação interativa no esquemático. Esta primeira
etapa prepara a base original, a compilação no Fedora e o simulador existente.
Os controles de simulação interativa descritos no mapa são etapas futuras.

- Fork: <https://github.com/ViniciusJesus-art/CircuitAliveEDA>
- Branch de trabalho: `circuitalive/main`.
- Base KiCad 10.0: `6809ada8746084720c0093eb484c889afb35d0d4`.
- Fontes diretamente na raiz deste repositório.
- [Configuração, compilação e execução](docs/circuitalive/BUILD_FEDORA.md).
- [Mapa do projeto](docs/circuitalive/Mapa_KiCad_Simulacao_Interativa.md).

Com o ambiente desta máquina preparado:

```bash
tools/circuitalive/configure.sh
tools/circuitalive/build.sh
tools/circuitalive/install.sh
./circuitalive
```

Para abrir apenas o editor de esquemáticos ou usar a linha de comando:

```bash
./circuitalive eeschema
./circuitalive cli version
```

Os binários, dependências locais e configurações ficam em `.circuitalive/`;
os objetos de compilação ficam em `build/circuitalive/`. Esses arquivos gerados
não são enviados ao Git. As bibliotecas do Fedora em `/usr/share/kicad` são
reutilizadas. As licenças e os créditos do KiCad e das dependências permanecem
nos arquivos originais do repositório.

---

# KiCad README

For specific documentation about [building KiCad](https://dev-docs.kicad.org/en/build/), policies
and guidelines, and source code documentation see the
[Developer Documentation](https://dev-docs.kicad.org) website.

You may also take a look into the [Wiki](https://gitlab.com/kicad/code/kicad/-/wikis/home),
the [contribution guide](https://dev-docs.kicad.org/en/contribute/).

For general information about KiCad and information about contributing to the documentation and
libraries, see our [Website](https://kicad.org/) and our [Forum](https://forum.kicad.info/).

## Build state

KiCad uses a host of CI resources.

GitLab CI pipeline status can be viewed for Linux and Windows builds of the latest commits.

## Release status
[![latest released version(s)](https://repology.org/badge/latest-versions/kicad.svg)](https://repology.org/project/kicad/versions)
[![Release status](https://repology.org/badge/tiny-repos/kicad.svg)](https://repology.org/metapackage/kicad/versions)

## Files
* [AUTHORS.txt](AUTHORS.txt) - The authors, contributors, document writers and translators list
* [CMakeLists.txt](CMakeLists.txt) - Main CMAKE build tool script
* [copyright.h](copyright.h) - A very short copy of the GNU General Public License to be included in new source files
* [Doxyfile](Doxyfile) - Doxygen config file for KiCad
* [INSTALL.txt](INSTALL.txt) - The release (binary) installation instructions
* [uncrustify.cfg](uncrustify.cfg) - Uncrustify config file for uncrustify sources formatting tool
* [_clang-format](_clang-format) - clang config file for clang-format sources formatting tool

## Subdirectories

* [3d-viewer](3d-viewer)         - Sourcecode of the 3D viewer
* [bitmap2component](bitmap2component)  - Sourcecode of the bitmap to PCB artwork converter
* [cmake](cmake)      - Modules for the CMAKE build tool
* [common](common)            - Sourcecode of the common library
* [cvpcb](cvpcb)             - Sourcecode of the CvPCB tool
* [demos](demos)             - Some demo examples
* [doxygen](doxygen)     - Configuration for generating pretty doxygen manual of the codebase
* [eeschema](eeschema)          - Sourcecode of the schematic editor
* [gerbview](gerbview)          - Sourcecode of the gerber viewer
* [include](include)           - Interfaces to the common library
* [kicad](kicad)             - Sourcecode of the project manager
* [libs](libs)           - Sourcecode of KiCad utilities (geometry and others)
* [pagelayout_editor](pagelayout_editor) - Sourcecode of the pagelayout editor
* [patches](patches)           - Collection of patches for external dependencies
* [pcbnew](pcbnew)           - Sourcecode of the printed circuit board editor
* [plugins](plugins)           - Sourcecode for the 3D viewer plugins
* [qa](qa)                - Unit testing framework for KiCad
* [resources](resources)         - Packaging resources such as bitmaps and operating system specific files
    - [bitmaps_png](resources/bitmaps_png)       - Menu and program icons
    - [project_template](resources/project_template)          - Project template
* [scripting](scripting)         - Python integration for KiCad
* [thirdparty](thirdparty)           - Sourcecode of external libraries used in KiCad but not written by the KiCad team
* [tools](tools)             - Helpers for developing, testing and building
* [translation](translation) - Translation data files (managed through [Weblate](https://hosted.weblate.org/projects/kicad/master-source/) for most languages)
* [utils](utils)             - Small utils for KiCad, e.g. IDF, STEP, and OGL tools and converters
