# Extensão dos motores de microcontroladores

O objetivo é executar firmware e periféricos como blocos funcionais conectados
ao circuito elétrico, sem reproduzir os transistores internos. Não existe um
motor universal que execute AVR, PIC, ARM e ESP apenas trocando a pinagem.
O contrato elétrico e os blocos de protocolo podem ser reutilizados entre ISAs.

## Separação das responsabilidades

| Parte | Arquivo/contrato | Responsabilidade |
|---|---|---|
| Símbolo/encapsulamento | `.casim` | Pinos, parâmetros, arquivos de firmware e módulos |
| Perfil do chip AVR | `avr8js-devices.js` | Memórias, layout compatível e vetores |
| CPU/periféricos AVR | AVR8js + `avr8js-adapter.js` | Instruções, registradores, timers, ADC, EEPROM, UART |
| Registradores seriais AVR | `avr8js-serial.js` | SPCR/SPSR/SPDR, TWCR/TWSR/TWDR, flags, interrupções e multiplexação |
| USART elétrica | `avr8js-usart.js` | FIFO/buffer, 5–9 bits, paridade/erros, síncrona e MSPIM |
| Sistema e clocks | `avr8js-system.js` | Watchdog, seis sleeps, PRR, IVSEL, BOD e reset |
| Memórias não voláteis | `avr8js-nvm.js` | SPM, Flash/EEPROM, boot, fuses e lock bits |
| Analógico/captura | `avr8js-analog.js`, `avr8js-timers.js` | ADC automático, comparador, ICP1, clocks assíncronos e GTCCR |
| Protocolos elétricos | `mcu-serial.js` | SPI e I²C por transições/leituras de pinos, sem conhecer a ISA |
| Host | `createDevice(config)` | Tempo confirmado, reset, entradas e eventos de saída |
| Circuito | ngspice + drivers | Tensões, correntes, pull-ups, cargas e alimentação |

Cada dispositivo tem seu próprio contexto e suas imagens congeladas. Um periférico
serial usa apenas `read(pin)`, `drive(pin, estado, tempo)` e callbacks de conclusão.
O adaptador transforma a conclusão em flags/registradores/interrupções do chip.
Esses blocos não usam o relógio real, rede, arquivos ou comunicação direta entre
instâncias; dois micros se comunicam pelos fios resolvidos pelo ngspice.

## Escalonamento e desempenho

O compilador informa quais entradas estão ligadas a fontes ideais e quais pinos
estão sem conexão externa. Um timer interno não impõe consultas elétricas a cada
microssegundo quando sua entrada de captura está aberta e sem driver. Leituras
de pinos, captura variável, ADC, interrupções externas e interfaces seriais
continuam impondo as fronteiras necessárias. Configurar um driver ou pull-up no
pino aberto também restaura a observação elétrica.

O acelerador AVR decodifica instruções por tabela. Escritas GPIO sem dependências
de entrada podem ser calculadas em lote; os efeitos nos periféricos são aplicados
na ordem e no ciclo original de cada escrita. Leituras com hooks personalizados
e mudanças de configuração continuam no adaptador. Reescrever a configuração
inalterada de compare/modo de um timer síncrono não exige refazer seus eventos.
Laços puros de espera (`RJMP`, `DEC`/`BRNE` e `SBIW`/`BRNE`) podem contar várias
iterações por operação, preservando ciclos, registradores e flags e parando
antes do próximo evento de hardware.
Limites de ciclos gerados junto com o decodificador permitem aproveitar os
últimos ciclos antes de cada evento no caminho nativo. O acelerador não cruza
essa fronteira; interrupções, `SEI`/`RETI` e acessos a periféricos continuam sendo
entregues ao adaptador nos ciclos corretos.

Redes comprovadamente sem estado elétrico reutilizam soluções do ngspice para
as mesmas entradas, preservando os dois lados das transições de GPIO. Se suas
fontes são DC, avançam entre eventos, sem repetir a grade analógica em intervalos
nos quais todas as tensões permanecem constantes. Capacitores,
indutores, transistores e modelos dinâmicos mantêm a integração transiente.
Vínculos ópticos são dependências explícitas: os pares vinculados permanecem no
mesmo domínio, enquanto circuitos independentes podem executar em outros motores.
Periféricos nativos também contam como trabalho próprio no agrupamento automático,
mesmo quando sua representação elétrica é pequena.
Um domínio sem armazenamento elétrico e sem firmware, alimentado por uma única
senoide contínua, também pode reutilizar soluções por fase. A grade respeita o
passo máximo; eventos dos periféricos mantêm seus próprios instantes. Fontes
misturadas, atraso, amortecimento e redes com memória usam o solver transiente.
O histórico transferido contém os sinais necessários ao osciloscópio e aos avisos;
os indicadores recebem seus valores atuais e a atividade de cada sinal.
Os avisos usam mínimos e máximos de todos os pontos aceitos no intervalo,
inclusive quando o buffer de histórico já sobrescreveu amostras antigas. O
osciloscópio recebe seus canais históricos. Para fontes replicadas entre motores,
somar picos independentes fornece apenas um limite: se ele puder exceder o
limite de corrente, o controlador recupera as curvas originais e calcula a soma
nos mesmos instantes, evitando alarmes por picos que não ocorreram juntos.
Trechos exatamente constantes desse histórico são representados pelos extremos,
preservando a interpolação e as transições. O áudio e a atividade continuam
recebendo todos os pontos aceitos pelo motor.

## Adicionar um chip compatível com o layout ATmega-x8

1. Conferir instruções, endereços de registradores, periféricos, memórias e vetores
   no datasheet. A semelhança do nome ou encapsulamento não comprova compatibilidade.
2. Criar um módulo de perfil, carregado depois do bundle AVR:

   ```javascript
   CircuitAliveAVR.registerDevice('meu_alvo_validado', {
     family: 'atmega-x8',
     flash: 32768, sram: 2048, eeprom: 1024, vectorDivisor: 1
   });
   ```

3. Criar o `.casim` com `device: "meu_alvo_validado"`, capacidades correspondentes,
   pinos do encapsulamento e `modules: ["../runtime/avr8js.js", "meu-perfil.js"]`.
4. Compilar firmware para o alvo e testar reset, instruções, interrupções e cada
   periférico anunciado. Registrar apenas capacidades que foram verificadas.

A API recusa IDs repetidos e outros layouts. O registro não permite selecionar
arbitrariamente outro ARM/AVR: novos mapas de registradores precisam de código.
`CircuitAliveAVR.capabilities` informa as interfaces seriais e lacunas atuais.

## Adicionar outra família

Criar um módulo que implemente o contrato de
[`createDevice(config)`](../../simulation/runtime/README.md), usando um núcleo
de CPU compatível com a ISA e os periféricos exatos do dispositivo. Para reutilizar
os blocos seriais, o `.casim` pode carregar `mcu-serial.js` antes desse módulo.
O novo adaptador fornece os pinos, taxas e callbacks adequados aos registradores
da família. A ponte elétrica e o protocolo do worker continuam os mesmos.

O contrato atual aceita módulos JavaScript síncronos em QuickJS. Integrar um
motor nativo ou um processo como QEMU/Renode exige outra integração no host e
empacotamento para Linux/Windows; isso não é habilitado por um perfil JSON.
STM32, ESP e novos PIC precisam dessa implementação e de testes próprios.

## Alimentação funcional

Os parâmetros do símbolo `supply_min`, `supply_max` e `supply_current` definem
a faixa de execução e uma carga aproximada. O consumo de base é:

```text
Ibase = Iconfigurado × min(1, max(0, VCC / Vmin))
```

Acima de Vmin, o consumo de base é constante. Abaixo dela, há uma carga resistiva
equivalente; em 0 V a corrente é zero. O consumo externo dos pinos é adicional e
vem da alimentação real dos drivers. Os valores padrão são exemplos ajustáveis,
não curvas de consumo de datasheet. RESET não desliga essa carga.

A CPU funciona dentro da faixa de tensão. Se uma fonte não sustenta a corrente,
a queda de tensão calculada pelo circuito pode interromper a CPU e liberar os
pinos. Não há uma comparação isolada que exija medir uma corrente mínima para
autorizar a execução. Os modelos antigos sem o parâmetro de consumo são mantidos.

## Cobertura funcional AVR

| Recurso AVR | Situação |
|---|---|
| GPIO, pull-up, interrupções, timers/PWM, ADC, EEPROM, UART assíncrona | Implementados, com os limites do contrato |
| SPI | Controlador/alvo, quatro modos, duas ordens de bits, seleção, flags e interrupções |
| I²C/TWI | Controlador/alvo de 7 bits, máscara/general call, ACK/NACK, leitura/escrita, repeated START, STOP, stretching |
| Arbitragem I²C | SDA resolvida, sincronização SCL e reconhecimento de endereço após perda de arbitragem |
| Sleep e watchdog | Seis modos com gating/wake-up; watchdog com WDR, interrupção, reset e modo combinado |
| Flash/EEPROM | Buffer de página, erase/write, SPM, RWW/NRWW, locks, busy/ready e restauração das imagens no reset de sessão |
| ADC automático | Free-running e todas as fontes ADTS, ADCL/ADCH atômicos e referências |
| Comparador e captura | AIN0/AIN1/bandgap/ACME, interrupções, ACIC, ICP1 e filtro de quatro ciclos |
| Timer2 assíncrono | Oscilador ideal configurável, EXCLK em PB6, ASSR, GTCCR e prescalers |
| USART | 5–9 bits, FIFO/buffer, paridade/frame/overrun, MPCM, síncrona e MSPIM |
| Fuses e boot | CKDIV8, CKOUT, RSTDISBL/PC6, WDTON, BOD, BOOTSZ/BOOTRST, leituras e locks |
| Clocks/consumo | Frequência nominal ideal, ajuste OSCCAL linear aproximado e carga de consumo fixa configurável |
| Programação/debug externos | ISP, alta tensão e debugWIRE não integram o motor de execução de firmware |
| STM32/ESP e outros PIC | Motores ainda não integrados |

O PIC16F84A mantém sua cobertura anterior; novos blocos AVR não acrescentam
periféricos a esse PIC. Cobrir todas as funções de um chip exige validar cada
modo de seus periféricos, em vez de assumir cobertura por pertencer à família.

Para periféricos rápidos, configurar um período de entrada bem menor que meio
bit. Em I²C, cada subida de SCL depende da tensão elétrica confirmada; pull-ups,
capacitância e stretching podem alongar a transferência. Sem pull-up, o motor
aguarda uma linha válida e não inventa uma resposta. Comunicação SPI exige
configurar o nível ocioso de SCK antes de selecionar o alvo com SS.

## Verificação

```sh
python3 simulation/runtime/build-avr8js.py
node qa/tests/cli/test_mcu_serial.cjs
node qa/tests/cli/test_avr_serial.cjs
node qa/tests/cli/test_avr_peripherals.cjs
node qa/tests/cli/test_avr_decoder.cjs
node qa/tests/cli/test_avr_timing.cjs
cmake --build build/circuitalive --target qa_interactive_sim
build/circuitalive/qa/tests/spice/qa_interactive_sim --run_test=Programmable*
```

Os testes incluem dois firmwares AVR ligados pelo solver analógico, troca SPI nos
quatro modos, escrita/leitura I²C com repeated START, bootloader SPM com leitura de
volta, comparador, wake-up por watchdog e fontes com resistência suficiente/
insuficiente. Os testes JavaScript verificam os seis sleeps, watchdog, prescaler,
ADC/triggers/captura, USART/erros/MSPIM, EEPROM, fuses/locks, IVSEL, BOD/PC6/CKOUT,
arbitragem entre dois controladores I²C, flags, NACK, stretching e falta de pull-up.
Node é uma ferramenta de desenvolvimento; não é exigido na aplicação instalada.

Os registradores SPI/TWI seguem a documentação primária do
[ATmega48A/PA/88A/PA/168A/PA/328/P, Microchip](https://www.microchip.com/content/dam/mchp/documents/MCU08/ProductDocuments/DataSheets/ATmega48A-PA-88A-PA-168A-PA-328-P-DS-DS40002061B.pdf).
