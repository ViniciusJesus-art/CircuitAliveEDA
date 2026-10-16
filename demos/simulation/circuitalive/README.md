# CircuitAliveEDA: RC, chave e LED

`interactive_rc_led.kicad_sch` é a demonstração da etapa 2. Abra no editor
CircuitAliveEDA e use a barra Play/Pause/Stop. Clique em SW1 durante execução ou
pausa. A capacitância maior torna carga e descarga perceptíveis na tela.

```bash
./circuitalive eeschema demos/simulation/circuitalive/interactive_rc_led.kicad_sch
```

SW1 inicia fechada; C1 inicia em 0 V. D1 usa o modelo didático vermelho indicado
pelo valor `RED`. O resistor R2 oferece caminho de descarga, e R3 limita a
corrente do LED. Não é preciso acrescentar diretiva `.tran`.

`rc_switch_led.cir` é o circuito de teste do núcleo da etapa 1, com constantes de
tempo menores e uma fonte de controle explícita; ele não é o esquemático da
interface.

Os símbolos incorporados ao arquivo `.kicad_sch` foram extraídos das bibliotecas
KiCad instaladas (`Device`, `Switch`, `Simulation_SPICE`), sob a licença das
bibliotecas KiCad, CC BY-SA 4.0 com a exceção para projetos que as utilizam.
Os modelos didáticos inline e o exemplo CircuitAliveEDA foram escritos para o
projeto. Detalhes e limites em [EDITOR_SIMULATION.md](../../../docs/circuitalive/EDITOR_SIMULATION.md).
