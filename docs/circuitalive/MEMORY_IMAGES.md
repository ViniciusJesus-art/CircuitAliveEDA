# Arquivos de dados e inspeção de memória

Cada instância pode ter arquivos próprios. Em **Simulation model… → Data files…**, escolha uma imagem, o formato e o endereço de carga em hexadecimal. Confirme as duas janelas e salve o esquema. O botão só aparece se o `.casim` declarar recursos; resistores e outros modelos sem arquivos não ganham esses controles.

A seleção fica no campo oculto `CA.Resources` do símbolo, com caminho relativo quando o arquivo está dentro do projeto. O arquivo continua externo: inclua-o ao compartilhar o projeto. Os padrões salvos para novos símbolos copiam modelo, pinos e parâmetros, mas não copiam a imagem de uma instância para todas as outras.

## Modelos disponíveis

- **27C512-55**: `Memory_EPROM:27C512`, 65.536 bytes, endereços A0–A15, oito saídas, CE/OE ativos em zero, alimentação de 5 V e atraso de acesso configurável, inicialmente 55 ns. Sem imagem, o conteúdo apagado é `FF`. A memória conserva os dados ao cortar a alimentação; as saídas ficam desabilitadas. A programação em alta tensão não é simulada.
- **CY62256-70PC**: SRAM de 32.768 bytes, barramento bidirecional e controles CS/OE/WE. Pode começar com uma imagem; posições não fornecidas começam indefinidas (`X`). As escritas do circuito alteram o estado em execução. A perda de alimentação invalida os dados. A imagem inicial só é reaplicada ao reiniciar a sessão com Stop/Play.

São aproximações comportamentais nominais. Os 55 ns são a configuração da variante solicitada; os níveis elétricos e a pinagem seguem a família 27C512. Os parâmetros de saída continuam sendo resistência e quedas nominais, sem reproduzir curvas completas nem programação física. Referência da família: [datasheet Microchip AT27C512R](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/DataSheets/doc0015.pdf). As variantes comerciais desse documento têm outros tempos; o parâmetro pode ser ajustado ao componente desejado.

## Ver e exportar

Clique com o botão direito em um componente com regiões de memória e escolha **View memory…**.

Antes de Play, a janela mostra a imagem inicial. Durante uma sessão, o comando pausa a simulação e captura o conteúdo real da região, com o instante em segundos. A captura não altera os pinos, não percorre o barramento e não avança o tempo. **Capture paused state** repete a leitura do estado pausado. Ao fechar a janela, use Play para continuar.

A tabela virtual mostra endereços, 16 bytes por linha e ASCII, com busca por endereço hexadecimal. `?` indica um nibble com bits indefinidos; `??` não significa `00` nem representa a alta impedância dos pinos. O visor é de leitura: para mudar o estado inicial, escolha outra imagem em Data files e reinicie a sessão.

**Export…** oferece:

| Formato | Uso |
|---|---|
| `.bin` | Bytes consecutivos; exige que todos os bits sejam conhecidos. |
| Intel HEX (`.hex`, `.ihex`) | Endereços e checksum; permite imagens esparsas. Na exportação, bytes inteiramente X são omitidos; bytes parcialmente conhecidos exigem CAMEM. |
| `.camem` | Snapshot CircuitAlive que conserva dados e máscara de bits conhecidos, inclusive X; pode inicializar outra RAM. |

BIN carrega no endereço escolhido. Intel HEX soma esse endereço aos endereços dos registros; o padrão é zero. CAMEM carrega dados e máscaras a partir desse endereço. Entradas com checksum inválido, registros conflitantes, fim ausente ou endereços fora da capacidade são rejeitadas antes de iniciar a simulação.

Exemplo pronto: abra `demos/simulation/circuitalive/memory_images.kicad_pro`. As imagens acompanham o projeto em `memory_images/`.

## Contrato modular dos modelos

O próprio `.casim` declara os recursos e as regiões, sem lista paralela de componentes autorizados:

```json
"resources": [
  {
    "id": "image",
    "label": "Initial memory image",
    "kind": "memory_image",
    "formats": ["bin", "ihex", "camem"],
    "required": false,
    "max_bytes": 8388608
  }
],
"memories": [
  {
    "id": "main",
    "label": "SRAM 32K x 8",
    "resource": "image",
    "address_bits": 15,
    "word_bits": 8,
    "writable": true,
    "fill": -1
  }
]
```

`fill: -1` significa bits indefinidos; valores de 0 a 255 preenchem bytes conhecidos. Regiões usam palavras little endian, com `ceil(word_bits/8)` bytes por endereço; as duas memórias fornecidas usam palavras de oito bits. Os vetores dos blocos são LSB primeiro.

Um bloco comportamental `ram` com `"memory": "main"` vincula `data_in`, `data_out`, `address`, `write` e `select` à região. O bloco `rom` usa `memory`, `data_out`, `address` e `select`. O compilador verifica a largura e a capacidade. Templates SPICE podem usar `${memory:main}` para obter o slot do worker. Blocos RAM antigos sem `memory` continuam compatíveis com `d_ram`, mas não oferecem imagens nem inspeção do estado.

Os blocos novos usam `ca_memory`, um módulo XSPICE pequeno incluído na compilação. As escritas acontecem nos eventos digitais do ngspice. O worker mantém páginas compartilhadas e versões de estado para acompanhar tentativas e recuos do solver, libera versões antigas e só permite ler páginas quando o solver está parado. Não armazena um histórico completo da RAM a cada passo. Limites atuais: 1 MiB agregado e 128 regiões por circuito; a leitura usa páginas de até 4 KiB.

`resources` também permite descrever firmware, bitstreams ou dados de sensores com seus próprios nomes, formatos e arquivos obrigatórios. A seleção, persistência, validação de arquivo e organização em regiões já podem ser reutilizadas por futuros motores. Isso ainda não executa instruções de microcontroladores, HDL de FPGA ou protocolos de sensores: cada motor precisará implementar seu comportamento e publicar suas regiões de estado. Esses componentes não se tornam simuláveis apenas por anexar um BIN.

O código está isolado em `eeschema/sim/memory_*`, `runtime_memory.*`, `memory_cm/`, `eeschema/circuitalive/resource_dialog.*` e `memory_viewer.*`. A interface XSPICE incluída é da versão 47; KLU e CIDER são detectados na biblioteca vinculada. Mudanças de versão/ABI do ngspice exigem recompilar e validar esse módulo. Criar imagens ou novos modelos usando os blocos existentes não exige recompilar.
