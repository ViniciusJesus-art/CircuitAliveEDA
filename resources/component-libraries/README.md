# Componentes adicionais do CircuitAliveEDA

Esta pasta é a fonte versionada das bibliotecas adicionais incluídas nos próximos
instaladores. Símbolos, footprints e modelos 3D ficam independentes dos projetos
que originaram a importação. Não coloque aqui esquemáticos, placas ou arquivos ROM.

Coleção inicial, copiada do projeto Card Wall-8/Projetos/Wall8.pretty:

- `EDAC:395-040-520-201`: símbolo e footprint importados de SamacSys;
  modelo `395-040-520-201 - EDAC Card Edge Connector.STEP` fornecido no projeto.
- `27c801:M27C801-100F6`: símbolo, footprint e modelo importados de SamacSys.
- `Wall8:Card_Wall_Gold_Finger`: footprint personalizado do projeto.

As propriedades de fabricante, fornecedor, descrição e datasheet dos símbolos
originais foram preservadas. Os arquivos não declaram uma licença própria;
esta cópia preserva sua origem e não atribui a eles a licença do código do programa.

`aliases.json` mantém o apelido global antigo `395-040-520-201` funcionando.
Novas bibliotecas são descobertas pelos nomes dos arquivos `.kicad_sym` e das
pastas `.pretty`. Os caminhos distribuídos usam `${KICAD10_SYMBOL_DIR}`,
`${KICAD10_FOOTPRINT_DIR}` e `${KICAD10_3DMODEL_DIR}`.

Veja [o fluxo de incorporação de componentes](../../docs/circuitalive/CUSTOM_COMPONENTS.md).
