# UNO R3 com firmware e entrada analógica

Abra `uno_adc.kicad_sch` e inicie a simulação. O firmware real do ATmega328P já
está associado à instância A1 por um caminho relativo ao projeto. Nenhuma
ferramenta externa é necessária para executar este exemplo.

Clique no potenciômetro RV1 e mova o slider. O firmware lê ADC0/A0 e acende o
LED em D13 quando a tensão fica acima de metade da alimentação. A probe D13
mostra a saída. A alimentação é 5 V e o clock configurado é 16 MHz.

`uno_adc.hex` e `uno_adc.bin` contêm o mesmo código de máquina AVR,
montado a partir da sequência em `uno_adc.asm`. O arquivo ASM documenta o
programa; o simulador carrega o HEX/BIN diretamente, sem montar o ASM.

Para usar seu firmware, abra a configuração de simulação de A1, escolha
**Data files…** e substitua **Firmware (Flash)**. O firmware deve ser para
ATmega328P e usar periféricos suportados pelo motor. Consulte
[a documentação dos modelos programáveis](../../../../../docs/circuitalive/PROGRAMMABLE_MODEL_FILES.md).
