/**
 * ATtiny25/45/85 Timer/Counter1
 * Part of AVR8js
 * Reference: http://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-2586-AVR-8-bit-Microcontroller-ATtiny25-ATtiny45-ATtiny85_Datasheet.pdf
 */
import { CPU } from '../cpu/cpu';
type u8 = number;
type u16 = number;
export interface ATtinyTimer1Config {
    TCCR1: u8;
    GTCCR: u8;
    TCNT1: u8;
    OCR1A: u8;
    OCR1B: u8;
    OCR1C: u8;
    TIFR: u8;
    TIMSK: u8;
    ovfInterrupt: u8;
    compAInterrupt: u8;
    compBInterrupt: u8;
    TOV1: u8;
    OCF1A: u8;
    OCF1B: u8;
    TOIE1: u8;
    OCIE1A: u8;
    OCIE1B: u8;
    compPortB: u16;
    compPinA: u8;
    compPinB: u8;
    dividers: Record<number, number>;
}
export declare const attinyTimer1Config: ATtinyTimer1Config;
export declare class ATtinyTimer1 {
    private cpu;
    private config;
    private lastCycle;
    private tcnt;
    private tcntNext;
    private tcntUpdated;
    private ocrA;
    private ocrB;
    private ocrC;
    private divider;
    private updateDivider;
    private countingUp;
    private readonly OVF;
    private readonly OCFA;
    private readonly OCFB;
    constructor(cpu: CPU, config: ATtinyTimer1Config);
    private get tccr1();
    private get gtccr();
    private get CS();
    private get ctcMode();
    private get pwmA();
    private get pwmB();
    private get comA();
    private get comB();
    /** TOP = OCR1C in CTC/PWM modes, 0xFF in Normal mode */
    private get TOP();
    count: (reschedule?: boolean) => void;
    private phasePwmCount;
    private timerUpdated;
    private forceCompare;
    private updateCompPinNonPwm;
    private updateCompPinPwm;
    private updateCompConfig;
}
export {};
