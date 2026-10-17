# CircuitAliveEDA: RC, chave e LED

Todos os 12 esquemas desta pasta são verificados por
`tools/circuitalive/test-demos.sh`. Os demais exemplos em `demos/simulation/`
vieram do KiCad e vários exigem o simulador SPICE tradicional e suas bibliotecas.
Use `tools/circuitalive/test-demos.sh --audit` para gerar o relatório de
compatibilidade interativa.

Para abrir no gerenciador, use **File → Open Demo Project…** e escolha o
`.kicad_pro` do exemplo. Cada esquemático tem seu projeto; a janela do gerenciador
não mostra esquemáticos isolados. Veja também as
[associações de modelos](../../../docs/circuitalive/MODEL_ASSOCIATIONS.md).

`interactive_rc_led.kicad_sch` é a demonstração da etapa 2. Abra no editor
CircuitAliveEDA e use os ícones Play/Pause/Stop no final da barra superior. Clique em SW1 durante execução ou
pausa. A capacitância maior torna carga e descarga perceptíveis na tela.

```bash
./circuitalive eeschema demos/simulation/circuitalive/interactive_rc_led.kicad_sch
```

SW1 inicia fechada; C1 inicia em 0 V. D1 usa o modelo didático vermelho inicial.
O resistor R2 oferece caminho de descarga, e R3 limita a
corrente do LED. Não é preciso acrescentar diretiva `.tran`.

`standard_library_led.kicad_sch` é o exemplo mínimo com fonte de 5 V, R1 de
`470R`, um `Device:LED` com o nome padrão e GND. Todos vêm das bibliotecas padrão
do KiCad. R1 inclui campos `Sim.*` para comprovar que eles não impedem o uso do
resistor ideal. Não é necessário atribuir modelo SPICE aos componentes básicos.

`open_pins_auto_reference.kicad_sch` usa o símbolo real `rectifier_schlib:R`, uma
fonte DC de 5 V sem GND explícito e R2 com um terminal aberto. A sessão escolhe
automaticamente o negativo da fonte como referência. O terminal aberto de R2
fica próximo de 5 V, sem corrente de carga, e o LED do ramo principal continua
funcionando. Pinos abertos não precisam ser conectados para permitir Play.

`rc_switch_led.cir` é o circuito de teste do núcleo da etapa 1, com constantes de
tempo menores e uma fonte de controle explícita; ele não é o esquemático da
interface.

Os símbolos incorporados ao arquivo `.kicad_sch` foram extraídos das bibliotecas
KiCad instaladas (`Device`, `Switch`, `Simulation_SPICE`), sob a licença das
bibliotecas KiCad, CC BY-SA 4.0 com a exceção para projetos que as utilizam.
Os modelos didáticos inline e o exemplo CircuitAliveEDA foram escritos para o
projeto. Detalhes e limites em [EDITOR_SIMULATION.md](../../../docs/circuitalive/EDITOR_SIMULATION.md).

## Modelos por arquivo

`models/voltage_divider.casim` exemplifica um modelo próprio com três terminais e
um subcircuito SPICE, sem associação automática. Pode ser vinculado a um desenho
com três pinos pela ação **Simulation model...**. A pinagem física e o parâmetro
`ratio` são configuráveis; não exige recompilar.

Veja [Arquivos de modelo](../../../docs/circuitalive/MODEL_FILES.md).

## Medições e osciloscópio

`measurement_probes.kicad_sch` contém probes V, A, W e osciloscópio, além de
V/I/P selecionados em R1 e D1. Play abre Measurements; Oscilloscope mostra a
tensão contínua no ponto D. R2 mantém um pino aberto para demonstrar que isso
não impede a simulação.

`scope_rc.kicad_sch` acrescenta uma probe de osciloscópio chamada Capacitor ao
exemplo RC/chave/LED. Play abre diretamente a curva; clique em SW1 para ver a
descarga e use Pause para inspecionar o trecho conservado.

```bash
./circuitalive eeschema demos/simulation/circuitalive/measurement_probes.kicad_sch
./circuitalive eeschema demos/simulation/circuitalive/scope_rc.kicad_sch
```

Uso, convenções de sinal e limites em
[Medições e osciloscópio](../../../docs/circuitalive/MEASUREMENTS.md).

## Fontes, diodos e lógica digital

`analog_rectifier.kicad_sch` usa VSIN, 1N4007, capacitor e resistor. Play abre
as medições do diodo/capacitor e a aba de osciloscópio com o sinal retificado.

`digital_74hc00.kicad_sch` usa as cinco unidades de um 74HC00. A unidade E
alimenta as quatro portas. SW1/SW2 aterradas mantêm A/B em 0; abertas deixam
os pull-ups elevar as entradas a 1. Ative o botão após Stop para ver as cores
dos pinos. FLOAT é uma entrada sem driver, e a saída correspondente é X.

Veja [Componentes básicos e digital](../../../docs/circuitalive/BASIC_AND_DIGITAL_MODELS.md)
para os modelos, as convenções de Z/X e os limites elétricos.

`digital_74hc74_pipeline.kicad_sch` demonstra dois estágios de um 74HC74
no mesmo clock NE555 (~480 Hz). No painel Simulation selecione **Speed 0.001×**
e clique em SW1: Q1 recebe o dado na primeira borda de subida; Q2 recebe
o Q1 anterior e muda na seguinte. Pause e Step permitem inspecionar fatias
de tempo; o osciloscópio tem CLK, Q1 e Q2 (experimente 500 µs/div e trigger CLK).
Detalhes em [Clock e inspeção](../../../docs/circuitalive/SEQUENTIAL_LOGIC_AND_DEMOS.md).

- `memory_images.kicad_pro`: EPROM 27C512-55 e SRAM com imagens Intel HEX. Use Simulation model → Data files para escolher dados, e View memory no menu de contexto para inspecionar/exportar o estado pausado. As imagens de exemplo ficam em `memory_images/`.
