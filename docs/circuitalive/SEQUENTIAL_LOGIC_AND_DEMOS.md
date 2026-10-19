# Clock, inspeção da sequência e exclusão de componentes

## 74HC74 e 74HCT74

Em dois flip-flops ligados ao mesmo clock, ambos capturam na mesma borda de
subida. O segundo captura o **valor anterior** de Q do primeiro. Assim, uma
mudança em D atravessa os dois estágios em duas bordas, respeitando o atraso
de propagação dos modelos.

Os modelos HC/HCT74 usam histerese na entrada de clock: uma oscilação dentro
da faixa entre os limiares não gera outra borda. A entrada precisa retornar
ao nível baixo antes de aceitar outra subida. Dados, preset e clear continuam
aceitando estados desconhecidos. Os limiares são aproximações nominais; não
há cálculo de metastabilidade ou validação de setup/hold.

Essa ação Schmitt está descrita na
[folha de dados HC/HCT74 da Nexperia](https://assets.nexperia.com/documents/data-sheet/74HC_HCT74.pdf).
O compilador combina comparadores dos dois limiares, `adc_bridge` com
limiares em ordem crescente e um latch SR do
[ngspice](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf). Assim, a
histerese funciona também no ngspice 46 usado no Windows, que não oferece o
modo Schmitt por inversão dos limiares do `adc_bridge`. A opção
`behavioral.clock_hysteresis: true` no `.casim` atua somente nas entradas
físicas conectadas ao clock de blocos `dff` daquele modelo.

## Ver sinais rápidos

No painel **Simulation**, selecione **Speed → 0.001×** para observar clocks de
centenas de hertz. O circuito mantém os mesmos valores elétricos, constantes
de tempo e atrasos; apenas avança menos tempo simulado por atualização.
A velocidade selecionada é conservada entre Stop e Play enquanto o editor
estiver aberto.

Com **Pause**, **Step** avança uma fatia de tempo mantendo a simulação
pausada e preservando seus estados. Cada fatia solicita `20 ms × Speed`,
com resolução limitada pelos passos aceitos pelo solver. Não é um avanço
automático até a próxima borda de clock. O tempo na barra inferior tem seis
casas decimais. O osciloscópio continua usando segundos de tempo simulado.

`digital_74hc74_pipeline.kicad_sch` tem NE555, dois estágios do mesmo 74HC74,
botão de dados e probes CLK/Q1/Q2. Os resistores de 100 kΩ e o capacitor de
10 nF dão aproximadamente 480 Hz. Em 1×, a atualização visual pode mostrar
ambas as saídas já alteradas. Use 0.001× ou o osciloscópio para ver a ordem.
Sem reset inicial, Q pode começar desconhecido até receber clocks válidos.

## Excluir uma instância

Selecione o componente, abra **Simulation model…** e marque
**Exclude this component from simulation**. Confirme e salve o esquema.

A exclusão usa a propriedade nativa do símbolo, compartilhada com a janela
de propriedades do KiCad. O componente é retirado antes da leitura e validação
de seu modelo, inclusive se o arquivo estiver ausente ou os campos estiverem
inválidos. A configuração anterior permanece salva; ao desmarcar, volta a
ser exigido um modelo válido. A exclusão não vira padrão para novos símbolos.
Em componentes com várias unidades, o comando atualiza as unidades colocadas
do mesmo componente.

Textos e caixas de texto, incluindo títulos iniciados com ponto, são ignorados
pela simulação interativa. Símbolos gráficos sem pinos também não precisam
de modelo. Diretivas SPICE em texto continuam pertencendo ao simulador SPICE
tradicional; elas não configuram a simulação interativa.

## Demos e testes

Os 12 esquemas em `demos/simulation/circuitalive/` são exemplos da simulação
interativa. A regressão carrega cada arquivo pelo leitor nativo do KiCad,
recalcula conexões, resolve modelos e executa um transiente. Há verificações
específicas das duas bordas, títulos e persistência da exclusão.

```bash
tools/circuitalive/build.sh --target qa_interactive_sim qa_spice
tools/circuitalive/test-demos.sh
tools/circuitalive/test-interactive.sh
```

O restante de `demos/simulation/` veio do KiCad. Esses exemplos usam bibliotecas
de fabricante, IBIS, blocos XSPICE ou sintaxe `Sim.*` para o simulador SPICE
tradicional. Não são todos compatíveis com Play interativo. O exemplo
`circuitalive/analog_rectifier.kicad_sch` é a versão interativa de retificação;
`circuitalive/linear_regulators.kicad_sch` demonstra LM317 e 7805.

```bash
tools/circuitalive/test-demos.sh --audit
```

A auditoria percorre também os exemplos originais e registra o primeiro
impedimento de cada esquema em `.circuitalive/logs/demo-audit.tsv`.
`requires_adaptation` significa que o exemplo precisa de associação/modelo ou
parâmetros próprios para Play; não certifica falha no simulador SPICE original.
A auditoria é informativa para os exemplos herdados. Falhas nos exemplos do
CircuitAlive fazem o teste retornar erro.
