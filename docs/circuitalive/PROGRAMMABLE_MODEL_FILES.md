# Arquivos de modelos programáveis

O modelo programável combina o circuito elétrico `.casim`, um módulo JavaScript
externo e o firmware escolhido por instância. O worker usa uma cópia congelada
desses arquivos em cada Play: editar um módulo ou trocar o firmware não exige
recompilar o CircuitAliveEDA e passa a valer na próxima simulação.

O contrato do runtime é independente da arquitetura. Os módulos fornecidos
são AVR8js para ATmega48P, ATmega88P, ATmega168P e ATmega328P e um interpretador
PIC16F84A independente. STM, ESP, FPGA e outros dispositivos
precisam de motores e modelos próprios; a presença do símbolo na biblioteca não
indica que seu firmware já possa ser executado.

## Escolher firmware e configurar o componente

Use o símbolo correspondente ao modelo exato. Os modelos fornecidos se associam
automaticamente aos símbolos locais `MCU_Microchip_ATmega:ATmega328P-P`,
`ATmega328P-A`, `ATmega328P-M`, `ATmega328P-MM`, variantes P/A/M de 48P/88P/168P
existentes na biblioteca e `MCU_Module:Arduino_UNO_R3`.

Na configuração do modelo de simulação, escolha os arquivos de dados do
componente. **Firmware (Flash)** é obrigatório; aceita Intel HEX (`.hex` ou
`.ihex`) e BIN. **Initial EEPROM image** é opcional. Os arquivos são associados à
instância do símbolo por `CA.Resources`, com caminhos relativos quando estiverem
dentro do projeto. Duas instâncias podem usar firmwares e clocks diferentes.

Configure **CPU clock** conforme o firmware foi compilado. O padrão é 16 MHz;
nenhum cristal externo precisa ser simulado nesta versão. **Electrical input
sampling period** limita o intervalo entre leituras do circuito: o padrão é
1 µs e pode ser reduzido para sinais externos mais rápidos. Isso não depende
da atualização da interface. As transições de saída conservam os tempos do
runtime; a amostragem das entradas tem essa granularidade configurada.

Os modelos fornecidos também expõem **Minimum operating supply voltage**,
**Maximum operating supply voltage** e **Approximate supply current**. O consumo
é uma carga funcional ajustável (padrões de exemplo: 5 mA no AVR e 2 mA no PIC),
não uma especificação de consumo do fabricante. Os drivers dos pinos consomem
adicionalmente a corrente exigida pelo circuito externo. Abaixo da tensão mínima,
a carga se torna proporcional à tensão para não exigir corrente ideal de uma
fonte desligada. Ela permanece durante RESET; estados de baixo consumo
usam gating funcional de clocks, mantendo essa carga aproximada constante. Uma fonte com resistência/limite insuficiente sofre
queda de tensão e o runtime interrompe a CPU quando sai da faixa configurada.

HEX contém endereços absolutos da Flash. BIN usa o endereço de carga informado
em hexadecimal, normalmente `0` para uma aplicação AVR. Regiões fora da memória
do alvo, arquivos vazios e checksums Intel HEX inválidos são recusados. Bytes
não presentes na imagem iniciam em `FF`. O firmware deve corresponder à família
e ao clock escolhidos; carregar um arquivo não identifica sua arquitetura.

No chip avulso, conecte VCC, AVCC e GND conforme o dispositivo. Os pinos de GND e
VCC duplicados do encapsulamento são ligados internamente. RESET tem pull-up
fraco para VCC; não é preciso ligá-lo permanentemente em nível alto. Um RESET
externo baixo desabilita os drivers e reinicia a execução quando liberado. AREF
tem um caminho fraco para VCC quando não existir referência externa. O relógio é
ideal e sua frequência nominal vem de **CPU clock**. Fuses são configuráveis em
decimal nos parâmetros `fuse_low`, `fuse_high`, `fuse_extended` e `lock_bits`.
CKDIV8 divide a frequência; CKOUT aciona PB0; RSTDISBL permite usar RESET como PC6;
WDTON, BOD e BOOTSZ/BOOTRST afetam o comportamento correspondente. O Timer2
assíncrono usa `timer2_hz` (32768 Hz por padrão), ou bordas externas em PB6/EXCLK.

No Arduino UNO R3, alimente **+5V** e **GND**. VIN, reguladores de 3V3 e interface
USB não são simulados. IOREF acompanha +5V. SDA/A4 e SCL/A5 duplicados são aliases
elétricos dos mesmos GPIOs. A placa usa o runtime ATmega328P com clock padrão
16 MHz; não executa o firmware do controlador USB.

## Schema 2

Modelos analógicos continuam usando `schema_version: 1`, `backend: "ngspice"`.
Modelos programáveis usam obrigatoriamente `schema_version: 2` e
`backend: "ngspice+runtime"`; versões antigas não podem tratá-los silenciosamente
como um conjunto de resistores. Exemplo da extensão:

```json
{
  "schema_version": 2,
  "backend": "ngspice+runtime",
  "runtime": {
    "api_version": 1,
    "engine": "javascript",
    "device": "atmega328p",
    "modules": ["../runtime/avr8js.js"],
    "clock_parameter": "clock_hz",
    "firmware_resource": "firmware",
    "firmware_bytes": 32768,
    "eeprom_resource": "eeprom",
    "eeprom_bytes": 1024,
    "vcc": "vcc",
    "gnd": "gnd",
    "min_supply": 1.8,
    "max_supply": 5.5,
    "min_supply_parameter": "supply_min",
    "max_supply_parameter": "supply_max",
    "supply_current_parameter": "supply_current",
    "options": {},
    "pins": [
      {"id": "PB5", "port": "pb5", "drive": true},
      {"id": "VCC", "port": "vcc"},
      {"id": "GND", "port": "gnd"},
      {"id": "RESET", "port": "reset"},
      {"id": "AREF", "port": "aref"}
    ]
  }
}
```

Esse fragmento precisa dos campos usuais de identidade, pinos, parâmetros,
recursos e SPICE. `port` identifica um port do modelo e não o número físico do
símbolo; `id` identifica o sinal visto pelo módulo. IDs são únicos por instância.
O mapa normal `CA.Pins` continua permitindo adaptar o modelo a outro símbolo
compatível. Não associar encapsulamentos só pela semelhança do desenho.
Os três campos `*_parameter` de alimentação são opcionais e devem apontar para
parâmetros declarados. Sem eles, `min_supply`/`max_supply` continuam definindo a
faixa e nenhuma carga adicional é criada, mantendo modelos externos existentes.

Os módulos são concatenados na ordem declarada, com caminhos relativos ao
arquivo `.casim`. O runtime recebe `device`, `clockHz`, `firmware`, `eeprom`,
`pins` e `options`. A API JavaScript é descrita em
[contrato do runtime](../../simulation/runtime/README.md). O módulo pode ser
substituído ou acrescido para implementar outra arquitetura sem mudar o
protocolo elétrico; um mapa de pinos sozinho não implementa uma CPU.

`runtime.option_parameters` mapeia nomes de opções para parâmetros numéricos
declarados, por exemplo `{"fuseHigh":"fuse_high", "timer2Hz":"timer2_hz"}`.
Esses valores são congelados por instância no Play, junto com firmware e módulos.
Um pino pode usar `drive_when_parameter`, `drive_mask` e `drive_value` para criar
o driver somente quando `(parâmetro & máscara) == valor`. O RESET dos AVR usa a
condição `fuse_high & 128 == 0`; a faixa inteira da Flash continua pertencendo ao
dispositivo e as regras de boot vêm do perfil/fuses.

GPIO com `drive: true` ganha três comandos externos ao ngspice: saída alta,
saída baixa e pull-up. Os comandos acionam caminhos com resistência finita
(25 Ω para drivers, 30 kΩ para pull-up), e entrada/Hi-Z libera ambos os drivers.
Cada sinal de entrada é medido em relação ao GND do modelo. A fonte de energia
do driver continua sendo a alimentação do circuito, não uma fonte ideal
independente que gere 5 V com o chip desligado.

O editor de modelos permite abrir e salvar modelos com runtime, editar seus
vínculos e suas disposições por símbolo, preservando o runtime. A composição
desses modelos como blocos elétricos continua indisponível nesta
versão, para não salvar um chip sem sua CPU. Edite o descritor e o módulo em
arquivos separados; o fluxo normal de associação/configuração no esquema aceita
os modelos programáveis.

## PIC16F84A

Os símbolos locais `MCU_Microchip_PIC16:PIC16F84A-XXP`, `PIC16F84A-XXSO` e
`PIC16F84A-XXSS` possuem modelos automáticos. Alimente VDD e VSS com 5 V; MCLR
tem pull-up interno do modelo. O clock padrão é 4 MHz de oscilador, com uma
instrução básica a cada quatro ciclos, conforme o PIC.

A Flash tem 1024 palavras de 14 bits; o BIN/HEX usa dois bytes little-endian por
palavra. O carregador aceita também as regiões HEX de identificação, configuração
(2007h, endereço de byte 400Eh) e EEPROM (2100h, byte 4200h). A área de endereços
aceita não equivale ao tamanho da Flash. O bit WDT deve estar desabilitado na
configuração; SLEEP e escrita de EEPROM ainda são recusados explicitamente.
GPIO, RA4 open-drain, pull-ups de PORTB, Timer0, interrupções e leitura de EEPROM
são implementados. O oscilador e o consumo físico não são simulados.

## Limites do backend AVR

O módulo AVR inclui instruções, GPIO/direção/pull-up, timers e PWM, interrupções,
ADC, EEPROM, USART, SPI e TWI/I²C com ponte elétrica. SPI implementa controlador
e alvo, quatro modos e duas ordens de bits. I²C implementa controlador e alvo de
7 bits, máscara de endereço/general call, ACK/NACK, repeated START, STOP e clock
stretching. O barramento precisa de pull-ups. Os tamanhos de memória e vetores de
interrupção diferem entre os quatro alvos e são configurados por dispositivo.
Também estão implementados watchdog com reset/interrupção/WDR, seis modos de
sleep com wake-up, PRR/GTCCR, comparador analógico, captura do Timer1 com filtro,
ADC automático, USART de 9 bits/síncrona/MSPIM, Timer2 assíncrono e autoprogramação
SPM com buffer/erase/write/RWW/locks. Fuses escolhem boot, reset, watchdog e BOD.
I²C sincroniza clocks e detecta arbitragem; o comparador de endereço do hardware
é de 7 bits e sequências de 10 bits precisam de firmware.
O modelo usa clocks ideais e consumo aproximado. Programadores externos ISP/alta
tensão e debugWIRE não fazem parte do motor de execução de firmware. Consulte
os detalhes de timing e capacidades no contrato do runtime.

Play/Stop inicializa as imagens originais; escrever na EEPROM simulada não
reescreve o arquivo escolhido no computador. Os arquivos `.casim`, módulos JS e
imagens são comuns ao Windows e ao Linux; o interpretador é distribuído com o
aplicativo de cada plataforma, sem exigir Node.js no computador do usuário.

## Referências de pinagem e capacidade

- [ATmega48P/88P/168P, Microchip](https://ww1.microchip.com/downloads/en/DeviceDoc/ATmega48P_88P_168P-DS40002065A.pdf).
- [ATmega328P, datasheet disponibilizado pela Arduino](https://content.arduino.cc/assets/ATmega328P_Datasheet.pdf).
- [UNO R3, pinout oficial](https://content.arduino.cc/assets/A000066-full-pinout.pdf).
- Símbolos distribuídos em `MCU_Microchip_ATmega.kicad_sym` e `MCU_Module.kicad_sym`.
