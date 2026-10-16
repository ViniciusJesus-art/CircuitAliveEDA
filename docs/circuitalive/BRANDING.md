# Nome e ícones do CircuitAliveEDA

A identidade visual usa os arquivos fornecidos em
`resources/bitmaps_png/icons/icon_kicad.png` (128 px), `icon_kicad_64.png`
(64 px) e `icon_kicad.ico` (vários tamanhos, incluindo 256 px).
Esses originais e os XPMs fornecidos são preservados.

Para atualizar os recursos depois de editar esses arquivos:

```bash
python3 tools/circuitalive/sync-branding-icons.py
tools/circuitalive/configure.sh
tools/circuitalive/build.sh
tools/circuitalive/install.sh
bash tools/circuitalive/install-desktop.sh
./circuitalive
```

O script requer Pillow (`python3-pillow` no Fedora). Ele reutiliza os pixels dos
arquivos fornecidos, gerando as variantes claras/escuras e os tamanhos usados
pela interface. Também atualiza os SVGs de origem com a imagem incorporada,
os ícones Linux e o ícone de projeto para Windows. As imagens da interface
são empacotadas em `images.tar.gz`; os ícones Linux são copiados pelo CMake
na configuração. Por isso os dois passos precisam ser atualizados.

`include/circuitalive/branding.h` reúne o nome, os títulos e o conjunto de
ícones de janela compartilhado pelos editores. Os ícones funcionais dos
botões de esquemático, PCB, bibliotecas e ferramentas conservam sua distinção.
Os nomes de janela, o gerenciador e o menu Sobre usam CircuitAliveEDA.

O atalho local `org.circuitaliveeda.CircuitAliveEDA.desktop` aponta para o
launcher deste checkout, com um caminho absoluto para o novo ícone. Sua
identidade é separada da instalação KiCad do sistema. Não modifica os atalhos
do KiCad instalado pelo Fedora. Feche o editor e o gerenciador antes de abrir
novamente, pois os ícones são conservados em memória.

Extensões `.kicad_*`, IDs de bibliotecas, variáveis `KICAD_*`, executáveis,
formatos, licenças e créditos upstream continuam com seus nomes técnicos.
A janela Sobre identifica o CircuitAliveEDA como derivado do KiCad e conserva
os autores e links upstream. Report Bug aponta para o repositório do projeto.
