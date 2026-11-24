# Ajustes durante a simulação e seleção de modelos

Para acompanhar lógica sequencial, o painel **Simulation** também tem
**Speed** (1× até 0.001×) e **Step** durante Pause. Veja
[clock, velocidade e exclusão de componentes](SEQUENTIAL_LOGIC_AND_DEMOS.md).

## Potenciômetros e trimpots

Com **Play** ativo, clique no corpo do potenciômetro ou trimpot. Um painel
pequeno abre com um controle deslizante e um campo numérico em porcentagem.
Arraste o cursor ou digite a porcentagem e pressione Enter. As setas do campo
numérico também aplicam alterações imediatamente. Esc ou um clique fora fecha
o painel. O esquema mostra a posição atual acima do componente.

A posição muda no solver existente: o tempo continua e capacitores, memórias
e demais estados do circuito são preservados. As duas trilhas dos
potenciômetros duplos acompanham a mesma posição e permanecem eletricamente
separadas. Os pinos de montagem continuam isolados.

**0%** corresponde à extremidade inferior (pino 3); **100%**, à superior
(pino 1). Nos duplos, a segunda trilha usa os pinos 4/5/6. A resistência total
continua vindo de Value, por exemplo `10k`, salvo substituição nos parâmetros.

O parâmetro `position` em **Simulation model...** define a posição inicial,
entre 0 e 1. O ajuste durante a execução pertence àquela sessão: Stop → Play
recomeça com a posição configurada. Pausar conserva a posição; se ela for
alterada com a simulação pausada, a resposta elétrica prossegue em Resume.

## Navegador de modelos

Em **Simulation model... → Choose model...**, o catálogo tem:

- Árvore de tipos e famílias, como Analog → Operational amplifiers → TL07x.
- Busca por nome, ID, família, descrição ou símbolo da biblioteca.
- Lista com modelo, família e símbolos associados.
- Descrição e caminho do arquivo selecionado antes da confirmação.

A janela começa na família do modelo atual. Digitar na busca procura em todo
o catálogo; depois é possível restringir os resultados clicando numa família.
Várias palavras combinam critérios, por exemplo `74HC buffer`.
A lista é virtualizada e o índice de busca é construído uma vez ao abrir a
janela, evitando criar um controle visual para cada um dos milhares de modelos.

**Automatic** mantém a associação automática. **Load file...** e **Reload file**
continuam disponíveis para arquivos próprios. Abrir a busca e cancelar não muda
o modelo, os parâmetros ou a pinagem. O mesmo navegador é utilizado no editor
visual ao duplicar um modelo existente ou adicionar um bloco.

Os seletores curtos de terminais, pinagem, escalas do osciloscópio, trigger,
canais e cursores usam o mesmo controle de lista: clique para abrir, solte o
botão e clique no item desejado. As células do editor visual também usam esse
controle, incluindo as que permitem digitar valores próprios.

Na janela **Modelo de simulação**, **OK** aplica a escolha e os ajustes somente
ao componente selecionado. O botão **Tornar padrão**, no topo, grava a associação
do símbolo com o modelo, os valores padrão dos parâmetros existentes, a pinagem
e as opções de medição, recursos e exclusão. Não há caixa de seleção para salvar
essas configurações automaticamente.

Os valores e a pinagem são gravados diretamente no arquivo `.casim`. Assim, o
editor de modelos abre os mesmos dados atualizados; alterações posteriores no
editor também atualizam os padrões usados pelos componentes sem substituições
individuais. O funcionamento e as definições dos parâmetros são compartilhados;
os valores padrão, a pinagem e as demais opções ficam na variação de cada
símbolo, identificado pelo nome completo na biblioteca. Assim, um único modelo
de LED pode atender LEDs difusos e de alto brilho com valores padrão diferentes.
A janela do esquema altera valores de parâmetros existentes; novos parâmetros e mudanças no comportamento
continuam sendo feitos no editor de modelos.

## Editar um modelo existente e seus símbolos

No editor de modelos, **Arquivo → Abrir… (Ctrl+O)** abre o mesmo catálogo de
modelos. **Salvar (Ctrl+S)** atualiza o arquivo selecionado, conservando seu ID,
vínculos, metadados e as configurações de parâmetros já salvas para os símbolos.
**Abrir arquivo de modelo… (Ctrl+Shift+O)** permite escolher um `.casim` fora do catálogo.

**Salvar como…** cria outro arquivo com ID próprio. A cópia conserva a lista de
símbolos e suas disposições, mas começa com a associação automática desativada
para preservar os vínculos do original. Marque **Usar este modelo por padrão
para todos os símbolos vinculados** para torná-la o padrão de novas colocações.
Os componentes já colocados continuam com seus campos individuais.

Na aba **Símbolos vinculados**, adicione ou remova símbolos de biblioteca. O
seletor **Símbolo**, acima do ID, acompanha essa lista. Escolher um símbolo na
lista ou no seletor troca a pinagem física, o símbolo de referência e a disposição
dos botões, sliders e partes luminosas. O diagrama em blocos, os sinais elétricos
e as definições dos parâmetros públicos permanecem compartilhados. A aba
**Parâmetros** mostra os valores padrão do símbolo selecionado: editar um valor
não altera os outros símbolos vinculados. Novos símbolos começam com os padrões
gerais do modelo.

Ao adicionar um símbolo, o editor associa primeiro os pinos pelo nome, depois
pelo número e, por fim, pelos pinos restantes. Confira a aba **Pinos do componente**;
a lista de cada célula permite corrigir a associação. Escolher um número ocupado
troca os dois pinos. Os símbolos vinculados devem ter a mesma quantidade de pinos
físicos que os terminais do modelo compartilhado. Todas as unidades são consideradas.

As variações são gravadas em `symbol_variants`, por ID completo de biblioteca:

```json
"symbol_variants": {
  "Custom:Display": {
    "pin_map": { "anode": "2", "cathode": "1" },
    "parameter_defaults": { "forward_voltage": "2.1" },
    "controls": {},
    "segments": {
      "A": {
        "shape": "line",
        "geometry": {
          "start": [0.2, 0.3], "end": [0.7, 0.3],
          "thickness": 0.04, "filled": false
        }
      }
    }
  }
}
```

`parameter_defaults` substitui valores dos parâmetros existentes apenas para
esse símbolo; parâmetros sem substituição usam os padrões gerais do modelo.
Substituições individuais no esquema têm prioridade sobre os padrões do símbolo.
`pin_map` usa os IDs lógicos do diagrama. `segments` altera apenas a apresentação;
o sinal de cada segmento continua no modelo comum. `controls` usa `control:ID`
para botões e `adjust:ID` para sliders, com os retângulos visuais já existentes.
Arquivos antigos sem variações continuam usando suas posições e pinagem originais.
Modelos com runtime podem ser abertos e salvos preservando o módulo existente;
a composição desse runtime como um bloco elétrico continua indisponível.

## Metadados nos arquivos `.casim`

A organização vem de campos opcionais no próprio modelo:

```json
"category": "Analog/Passive components",
"family": "Potentiometers and trimmers"
```

A barra `/` cria níveis de categoria. Arquivos antigos sem esses campos continuam
válidos: a categoria vem do nome da biblioteca, ou de Custom quando não há
associação. Isso organiza a interface; a associação elétrica continua em
`matches`, sem uma lista paralela de componentes permitidos.

Ajustes contínuos também são declarados no arquivo, sem depender do nome do
componente. Exemplo dos potenciômetros:

```json
"adjustments": [
  {
    "id": "position",
    "label": "Wiper position",
    "source": "Vposition_${id}",
    "parameter": "position",
    "unit": "%",
    "min": 0,
    "max": 1,
    "step": 0.01
  }
]
```

`source` identifica uma fonte independente V/I do modelo. Seu valor inicial
vem do parâmetro indicado, e a interface altera essa fonte durante a execução.
O modelo deve usar o sinal da fonte em suas equações. Nos potenciômetros,
`Vposition_${id}` controla duas condutâncias variáveis; nos duplos controla
ambas as trilhas. O símbolo continua sendo o da biblioteca padrão.

`unit: "%"` apresenta valores normalizados de 0–1 como 0–100% na interface;
o solver recebe a razão, por exemplo 0,74. Outros ajustes usam a unidade e
faixa declaradas sem essa conversão. O parâmetro e os limites devem existir e
ser compatíveis. Um componente pode declarar mais de um ajuste no painel.

Movimentos consecutivos do mesmo controle são agrupados antes do envio ao
worker, mantendo o valor mais recente e a ordem em relação a Pause/Stop.
A interface não realiza operações bloqueantes com o ngspice.

Displays com segmentos posicionáveis, LCDs, som e associações ópticas estão
em [Displays, áudio e vínculos de simulação](SIMULATION_OUTPUTS_AND_LINKS.md).
