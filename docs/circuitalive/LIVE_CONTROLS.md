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
