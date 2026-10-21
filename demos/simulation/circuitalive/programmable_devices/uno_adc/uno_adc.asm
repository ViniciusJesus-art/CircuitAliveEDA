; ATmega328P, 16 MHz. Standard AVR assembler syntax; HEX/BIN already provided.
; I/O addresses below are offsets from 0x20; STS/LDS use data-space addresses.
    ldi r16, 0x20
    out 0x04, r16       ; DDRB: PB5/D13 output
    ldi r16, 0x40
    sts 0x007c, r16     ; ADMUX: AVCC reference, ADC0
convert:
    ldi r16, 0xc7
    sts 0x007a, r16     ; ADCSRA: enable, start, prescaler /128
wait_adc:
    lds r16, 0x007a
    sbrc r16, 6
    rjmp wait_adc
    lds r17, 0x0078     ; ADCL before ADCH
    lds r16, 0x0079
    sbrs r16, 1         ; ADC bit 9: half-scale threshold
    rjmp output_low
    sbi 0x05, 5         ; PORTB5 HIGH
    rjmp convert
output_low:
    cbi 0x05, 5
    rjmp convert
