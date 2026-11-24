# Displays, áudio e vínculos de simulação

Os dispositivos são modelos funcionais conectados ao circuito. As junções dos
segmentos e do LED IR têm corrente calculada pelo solver. O LCD usa um bloco
JavaScript isolado; o receptor IR usa um bloco funcional nativo, acionado pelas
mudanças elétricas e pelos seus próprios prazos de demodulação.
O editor recebe seu estado para desenhar os resultados. O áudio é reproduzido
pelo próprio editor.

## Componentes disponíveis

| Modelo / símbolo | Funcionamento |
| --- | --- |
| `Display_Character:SC39-11*`, `SC39-12*` | Display de sete segmentos, cátodo comum, mais ponto decimal. |
| `Display_Character:SA39-11*`, `SA39-12*` | Versão de ânodo comum. As séries 11 e 12 têm pinagens diferentes. |
| `CircuitAlive_Simulation:LED_11Segment_CC` | Onze segmentos independentes; geometria editável. |
| `Display_Character:WC1602A`, `RC1602A`, `HY1602E` | LCD HD44780 16×2 paralelo, interfaces de 4 e 8 bits. |
| `CircuitAlive_Simulation:LCD1602_I2C` | LCD com backpack PCF8574, quatro pinos: GND, VCC, SDA, SCL. |
| `Device:Buzzer`, `Speaker_Crystal` | Piezo passivo: reproduz o sinal aplicado. |
| `Device:Speaker` | Alto-falante com resistência e indutância de bobina configuráveis. |
| `CircuitAlive_Simulation:Buzzer_Active` | Oscilador interno: alimentação válida produz o tom configurado. |
| `LED:CQY99` / modelo `ir_led` | Emissor IR cuja intensidade segue a corrente direta real. |
| `Interface_Optical:TSOP331xx` | Receptor IR demodulado, saída ativa em nível baixo. |

A biblioteca `CircuitAlive_Simulation` participa da coleção de componentes do
programa e do sincronizador das instalações de desenvolvimento. Outros símbolos
podem usar esses modelos por **Simulation model…**, com a pinagem correspondente.
O valor “11” no nome comercial SC39-11 não indica onze segmentos: essa série tem
sete segmentos e ponto decimal. O modelo de onze segmentos é separado.

Use um resistor limitador por segmento. Os comuns duplicados são conectados
internamente; cada corrente é medida separadamente. Multiplexação rápida utiliza
o mesmo acompanhamento de atividade dos LEDs, preservando a fase acesa mesmo
quando o quadro visual coincide com a fase apagada.

## Desenhar novos segmentos

Abra um modelo no editor visual e escolha **Luminous segments**. Use **Add line**
ou **Add circle**. Arraste o corpo para mover e cada extremidade para definir a
posição e a inclinação; **Properties…** permite ajustar coordenadas, espessura,
cor, sinal observado e escala de brilho. As medidas são frações do corpo do
símbolo, seguindo o princípio das áreas dos botões e sliders.

Cada segmento pode observar uma tensão ou corrente independente. Por exemplo,
uma fonte de sensoriamento de 0 V em série com o diodo permite usar
`i(Vsense_${id})`. O brilho depende do sinal elétrico, não do caractere que se
pretende mostrar. O editor preserva esses sinais ao compor o núcleo elétrico.
Qualquer quantidade até 256 segmentos por componente é aceita, incluindo
arranjos de 7, 11, 14 e 16 segmentos.

```json
"segments": [{
  "id": "A", "signal": "i(Vsense_${id})", "full_scale": 0.01,
  "shape": "line", "red": 1, "green": 0.12, "blue": 0.02,
  "geometry": {"start":[0.2,0.1], "end":[0.8,0.16],
               "thickness":0.04, "filled":false}
}]
```

`full_scale_parameter` pode apontar para um parâmetro numérico do modelo. As
formas novas são `line` e `circle`. Arraste cada extremidade para posicioná-la
livremente; arraste o corpo para mover a figura inteira. Em círculos, as duas
extremidades definem o diâmetro. Em **Properties**, configure a espessura, cor,
preenchimento, visibilidade e sinal. As coordenadas são frações da largura/altura
do corpo do símbolo; a espessura é uma fração de sua menor dimensão. Modelos
antigos com `horizontal`, `vertical`, `slash`, `backslash` e `dot` continuam
carregando com a mesma geometria e são convertidos ao editá-los.
Sinais negativos não iluminam o segmento. A geometria acompanha a rotação e o
espelhamento do símbolo; o preview mostra o símbolo na orientação original.

No diagrama em blocos, nos segmentos luminosos e nos controles interativos,
gire a roda para ampliar/reduzir em direção ao ponteiro e arraste com o botão
central pressionado para deslocar a visão em qualquer direção.

## LCD 16×2

O símbolo `Display_Character:HY1602E` é associado automaticamente ao modelo
paralelo. Sua numeração difere da pinagem convencional do WC1602A: VSS=3,
VDD=4, VO=5, RS=6, R/W=7, E=8, DB0–DB7=9–16, LEDA=2 e LEDK=1. A variação
`symbol_variants` mantém essa pinagem no mesmo modelo compartilhado.

O HD44780 implementa DDRAM, CGRAM com oito caracteres personalizados, endereço,
incremento/decremento, deslocamento, cursor, piscar, display ligado/desligado,
busy flag e leitura dos dados. Inicializa em 8 bits; a sequência normal de
inicialização seleciona os 4 bits. Instruções durante busy são ignoradas.
Clear/home ocupam 1,52 ms; operações ordinárias usam 37 µs. Aguarde a
inicialização após energizar, como no componente real.

No paralelo, ligue RS, E e D0…D7 conforme a interface usada. R/W pode ser ligado
a GND para escrita somente; D4…D7 são usados em modo de 4 bits. Os dados são
bidirecionais durante leitura. VDD/GND devem estar na faixa de alimentação do
modelo; LED+/LED− controlam a indicação de backlight.

O backpack usa endereço de sete bits `0x27` (39 decimal), ajustável nos parâmetros.
O mapa padrão é P0=RS, P1=R/W, P2=E, P3=backlight e P4…P7=D4…D7. O módulo também
aceita outro mapa pelo objeto `runtime.options.mapping`. SDA/SCL são open drain:
**instale pull-ups externos**. Escritas, leituras, ACK/NACK e repeated START
passam pelo barramento simulado. O endereço configurado não inclui o bit R/W.

A apresentação usa os caracteres ASCII, grau e os oito padrões CGRAM. Não é uma
reprodução completa de todas as ROMs regionais do HD44780. O contraste e a
óptica são idealizados. A área do LCD pode ser ajustada em `display.visual`.
Com a simulação em execução ou pausada, arraste a tela do LCD com o botão
esquerdo para posicioná-la no esquema. Soltar o botão confirma a posição; Esc
cancela o movimento. Essa posição pertence ao componente, mantém o tamanho
definido pelo modelo e é reaplicada ao parar/iniciar. Salve o esquema para
mantê-la após fechar o projeto. O movimento não reinicia o firmware nem altera
o modelo compartilhado pelos outros componentes.

O controlador paralelo roda no motor nativo e observa os pontos aceitos pelo
solver, sem forçar uma consulta JavaScript a cada 1 µs. Os pulsos de E precisam
ser resolvidos pelo passo elétrico do circuito. O parâmetro `sample_period` é
mantido por compatibilidade, mas não limita o passo no LCD paralelo nativo.
No backpack I²C, esse parâmetro ainda limita o intervalo de observação do
barramento. O oscilador interno, a corrente de repouso e os tempos são modelos
funcionais. Não há emulação de transistores do controlador do display.

Em redes de displays de segmentos comprovadamente sem estado elétrico, o motor
reutiliza a solução para entradas iguais. Alterações nas fontes invalidam essa
solução. Redes com capacitores, indutores ou outros elementos com estado seguem
no solver transiente; as correntes dos LEDs continuam calculadas eletricamente.

## Vincular e desvincular componentes

Na barra lateral direita, **Link simulation components…** permite clicar no
emissor e no receptor. Quando falta espaço na barra, abra a setinha na parte
inferior para encontrar a ferramenta. Esc encerra a ferramenta.

Pelo menu de contexto: clique no corpo do LED IR, segure **Shift** e clique no
corpo do receptor; solte Shift, clique com o botão direito e escolha
**Link simulation components…**. Selecione somente os dois símbolos, sem incluir
fios, textos ou outros objetos. No exemplo com CQY99 e TSOP331xx, a associação
automática usa os modelos `ir_led` e `ir_receiver_tsop`, respectivamente.
Os modelos devem declarar o mesmo meio e papéis compatíveis. Um componente pode
ter vários vínculos, até 64.

**Unlink simulation components…** remove o vínculo entre dois selecionados.
Com um selecionado, remove todos os seus vínculos; com a seleção vazia, a
ferramenta permite clicar nos componentes a desvincular. Os comandos têm undo/redo.

A seleção principal mantém o destaque normal do editor. Os parceiros aparecem
contornados em verde. Com a simulação iniciada, uma linha verde conecta cada par
na folha atual. Vínculos entre folhas mantêm a comunicação; a linha aparece
quando as duas pontas estão visíveis na mesma folha.

Os vínculos ficam em `CA.Links`, usando o caminho da instância da folha e o UUID
do símbolo. Mover ou renumerar um componente preserva o vínculo. Copiar um símbolo
não cria automaticamente um vínculo novo; as duas pontas precisam concordar.
Componentes apagados ou excluídos da simulação não transmitem sinais.

### Primeiro meio implementado: IR

A corrente dos emissores vinculados alimenta uma entrada óptica virtual do
receptor, sem compartilhar alimentação ou conexões elétricas. A portadora padrão
é 38 kHz, configurável. O receptor exige pelo menos seis ciclos consecutivos na
faixa de frequência e solta a saída após a ausência da portadora. Luz contínua,
portadora incompatível e alimentação fora da faixa não produzem recepção válida.

É uma aproximação funcional: tolerância de frequência de 20%, timeout de cerca
de 1,7 períodos e rejeição de portadora contínua após 20 ms. Não há distância,
orientação, ambiente luminoso, AGC analógico completo ou ruído óptico. O próprio
firmware interpreta protocolos como NEC; o vínculo transporta a portadora e o
receptor entrega o envelope.

Para outros meios, os modelos podem declarar:

```json
"link": {"medium":"ir", "role":"transmit", "signal":"i(Vsense_${id})"}
```

```json
"link": {"medium":"ir", "role":"receive", "input":"IR"}
```

O runtime recebe `IR` como entrada virtual. Também existe o papel `transceiver`.
A infraestrutura pode atender RF, Wi-Fi e Bluetooth; seus rádios e protocolos
precisam de blocos próprios. Vincular componentes não implementa esses protocolos
nem acrescenta um emulador de CPU ESP/STM/PIC.

### Desempenho do IR

O receptor não executa uma CPU nem consulta um interpretador a cada microssegundo.
`runtime.engine: "native"`, com `device: "ir_demodulator"`, seleciona o bloco
funcional integrado. Seus parâmetros de portadora, corrente mínima e alimentação
continuam no `.casim`; ele dispensa módulos, firmware e `sample_period`.
Os vínculos nativos leem diretamente as correntes dos emissores, sem criar fontes
e nós ópticos auxiliares no circuito.

O agendador do editor também distingue esses periféricos dos dispositivos que
executam firmware. O limite de instruções/amostras por pedido só conta os
dispositivos com interpretador; adicionar receptores IR não reduz artificialmente
o avanço de cada quadro para dezenas de microssegundos.

Para redes compatíveis sem armazenamento de energia, o worker calcula com
ngspice a solução elétrica de cada combinação de entradas e saídas. Reutiliza
essa solução quando a mesma combinação se repete, avançando pelas bordas reais
do clock, pelo fim dos bursts e pelo timeout do receptor. Tensões, correntes,
resistências de saída e limiares de alimentação continuam sendo considerados.
O cache é limitado a 2048 estados e é invalidado quando um controle muda.

A seleção é conservadora: capacitores, indutores, modelos desconhecidos,
capacitância de junção declarada, firmware e dependências temporais incompatíveis
mantêm o solver transitório. As chaves aceitas nesse caminho têm controles DC
independentes; fontes comportamentais só podem ler fontes independentes.
`SIM_SESSION_CONFIG::accelerateStateless = false` força o caminho transitório
para comparações. Não se acelera o relógio da interface para mascarar atrasos.

Verificação elétrica e de desempenho:

```bash
build/circuitalive/qa/tests/spice/qa_interactive_sim \
  --run_test=SimulationOutputs/NativeInfraredClockMatchesTransientSolverAndPreservesCapacitorDynamics
build/circuitalive/qa/tests/spice/qa_interactive_sim \
  --run_test=SimulationOutputs/InfraredNativeThroughput --log_level=message
```

A segunda bancada mede uma portadora de 38 kHz, primeiro com um receptor e depois
com oito. A capacidade medida depende do circuito, das medições habilitadas e do
computador; a reprodução normal permanece cadenciada em 1×.

## Áudio no computador

O botão **Simulation audio** na barra superior liga/silencia a reprodução. Pause,
Stop e alterações no circuito interrompem o áudio. `volume` controla o volume do
modelo entre 0 e 1; o sistema operacional controla o dispositivo de saída.

O worker integra os passos elétricos em amostras mono de 48 kHz, preservando a
área dos pulsos, remove DC, mistura as saídas e limita a amplitude. O fluxo tem
buffers limitados e não depende da janela do osciloscópio. O editor reproduz em
uma thread própria: PulseAudio/PipeWire compatível no Linux e waveOut no Windows.
Uma saída indisponível aparece no status; a simulação elétrica continua.

O relógio de 48 kHz pertence ao conversor de áudio: adicionar um speaker não
impõe 96 mil passos elétricos por segundo ao circuito inteiro. As bordas das
fontes, a frequência dos sinais suaves e o erro do solver determinam os passos;
o conversor integra esses intervalos para gerar o PCM. No Linux, o reprodutor
recompõe 20 ms de áudio após faltar dados, permitindo retomar o som depois de
um atraso temporário da simulação.

A qualidade depende dos passos elétricos e da capacidade de acompanhar o tempo
real. Uma simulação que avance lentamente pode produzir intervalos no áudio;
PWM muito rápido exige passos compatíveis. Resistência/indutância/capacitância
são cargas elétricas funcionais; acústica, ressonâncias do gabinete e volume
físico em dB não são modelados.

```json
"audio": {"signal":"v(audio_${id})", "full_scale":5, "volume":0.2}
```

`full_scale_parameter` e `volume_parameter` permitem ajustes pelos parâmetros do
modelo. Novos periféricos com estado visual podem publicar `presentation()` no
runtime; o estado JSON é limitado a 16 KiB por dispositivo e acompanha somente
fronteiras confirmadas da simulação.

## Erro ao abrir a configuração de qualquer componente

Se a mensagem citar `ir_receiver_tsop.casim` e `key 'clock_parameter' not found`,
o editor instalado ainda é anterior ao suporte aos periféricos funcionais.
IR e LCD não executam firmware e podem omitir `clock_parameter` e
`firmware_resource`. A janela carrega o catálogo inteiro antes de selecionar
um modelo; por isso a falha aparece mesmo ao configurar um resistor ou botão.

Após compilar as mudanças, execute `tools/circuitalive/install.sh` e feche todas
as janelas do CircuitAliveEDA, incluindo o gerenciador, antes de reabri-lo.
O launcher padrão usa `.circuitalive/install`; apenas compilar não atualiza essa
instalação. `./circuitalive --build eeschema` executa diretamente a compilação.
Os modelos do checkout são carregados ao vivo, então o editor e o worker também
precisam estar atualizados para interpretar extensões novas do formato.

## Exemplo e referências

Abra [simulation_outputs_ir_audio.kicad_sch](../../demos/simulation/circuitalive/outputs_ir_audio/simulation_outputs_ir_audio.kicad_sch)
com o arquivo `ir_burst_source.casim` na mesma pasta. O exemplo mostra um dígito,
IR com os vínculos salvos e áudio senoidal. Selecione D1 para ver U1 em verde e
pressione Play para acompanhar os resultados.

Referências técnicas: [datasheet HD44780U](https://www.sparkfun.com/datasheets/LCD/HD44780.pdf),
[datasheet TSOP331](https://www.vishay.com/docs/82742/tsop331.pdf) e
[API simples do PulseAudio](https://freedesktop.org/software/pulseaudio/doxygen/simple.html).

Verificação: `qa_interactive_sim --run_test=SimulationOutputs` e
`node qa/tests/cli/test_simulation_devices.cjs`.
