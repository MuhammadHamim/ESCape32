#pragma once

#define CLK 96000000

// --- Interface timer: TIM2 (zero-cross capture on CC1+CC2, CC3 = internal OC/TRGO) ---
// NOTE: STM32G0 TIM2 is 32-bit, CH32V203 TIM2 is 16-bit. Check IFTIM_XRES and counter-width use in common code.
// Remap TIM2_RM = 10b (Partial Remap 2): CH1=PA0, CH2=PA1, while CH3_2/CH4_2 move to PB10/PB11,
// disconnecting CC3/CC4 from PA2/PA3 so PA2/PA3 remain completely free for USART2.
#define IFTIM TIM2
#define IFTIM_XRES 0 // CHANGED (was 2): TIM2 is 16-bit, main.c writes (1 << (IFTIM_XRES + 16)) - 1 to ARR, so XRES must be 0 (0.5us tick, 32ms max)
#define IFTIM_ICFL 64
#define IFTIM_ICMR (TIM2->CHCTLR1)
// IC filter codes (CHCTLR1 IC1F = bits 7:4, IC2F = bits 15:12): 0x9 = DTF/8 N=8, 0x7 = DTF/4 N=8, 0x5 = DTF/2 N=8
#define IFTIM_ICM1 (TIM_CC1S_0 | (0x9 << 4) | TIM_CC2S_0 | (0x9 << 12))
#define IFTIM_ICM2 (TIM_CC1S_0 | (0x7 << 4) | TIM_CC2S_0 | (0x7 << 12))
#define IFTIM_ICM3 (TIM_CC1S_0 | (0x5 << 4) | TIM_CC2S_0 | (0x5 << 12))
#define IFTIM_ICIE (TIM_CC1IE | TIM_CC2IE)
#define IFTIM_ICR (sr & TIM_CC1IF ? TIM2->CH1CVR : TIM2->CH2CVR)
#define IFTIM_OCR (TIM2->CH3CVR)

// --- IO (throttle) timer: TIM3 CH1_2 on PB4 (TIM3_RM = 10b partial remap) ---
#define IOTIM TIM3
// Moving TIM3_CH1 to PB4 (Pin 40) via TIM3_RM = 10b keeps PA6 (Pin 16) free for OPA1_CH1N / ADC_IN6.
#define IOTIM_IDR (GPIOB->INDR & (1u << 4)) // PB4
#define iotim_isr tim3_io_isr
// TIM3_CH1 generates the throttle DMA request on DMA1 channel 6.
#define IOTIM_DMA 6 // TIM3_CH1_2 (PB4) -> DMA1 channel 6

// --- USART1 DMA (Primary Serial / Telemetry: USART1_RM = 1) ---
#define USART1_TX_DMA 4 // PB6 (remapped) -> DMA1 CH4
#define USART1_RX_DMA 5 // DMA1 CH5. NOTE telem.c always uses half-duplex (HDSEL): only TX (PB6) is wired/used, PB7 is not touched

// --- USART2 (Secondary Serial: Default Mapping on PA2/PA3) ---
// NOTE: USART2_RX DMA shares DMA1 CH6 with TIM3_CH1 (IOTIM). Use IRQ-driven RX on USART2 while IOTIM DMA is active.
#define USART2_TX_DMA 7 // PA2 -> DMA1 CH7

// --- NEW: dead time (TIM1 BDTR.DTG ticks of CLK; defs.h converts to TIM_DTG) ---
// 48 ticks @ 96 MHz = 0.5 us. TODO: set from your gate-driver / MOSFET datasheet. Must be < 128 here.
#define DEAD_TIME 48

// --- NEW: comparator wiring (defs.h turns this into COMP_IN1..3, used by compctl() in config.c) ---
// 123 means: commutation phase code 1 -> board input 1 (phase A), 2 -> input 2 (B), 3 -> input 3 (C).
// If the motor refuses to run, the usual cause is a wrong phase order: try 231, 312, 132, 321, 213.
#define COMP_MAP 123

// --- NEW: ADC sensors (defs.h derives SENS_CNT from the SIZE of SENS_MAP: 1 byte per sensor) ---
// Order in the DMA buffer is: current (if 2+ sensors), voltage, extra/NTC (if 3 sensors).
// SENS_MAP only has to have the right number of bytes. SENS_CHAN packs the ADC channel numbers, 5 bits each,
// first listed sensor in the lowest bits.
//   1 sensor : voltage            2 sensors: current + voltage       3 sensors: + NTC
// TODO: PA5 = ADC_IN5 is the only sensing pin named in this file; replace with your real pins.
#define SENS_MAP 0x05 // 1 sensor: voltage on ADC_IN5 (PA5)
#define SENS_CHAN 5
// Two-sensor example (current on IN6?? NOT a free pin on your board, shown for the syntax only):
//   #define SENS_MAP 0x0605
//   #define SENS_CHAN (6 | (5 << 5)) // current = IN6, voltage = IN5
// Scaling, derived from main.c adcdata():
//   volt[V/100] = v_mV * VOLT_MUL / 100      ->  VOLT_MUL = 10 * (R_top + R_bottom) / R_bottom
//   curr[A/100] = c_mV * CURR_MUL / 10       ->  CURR_MUL = 1000 / sensitivity[mV per A]
#define VOLT_MUL 110 // TODO: 110 assumes a 10k/1k divider (ratio 11). Set from YOUR divider.
// #define CURR_MUL 100 // TODO if you add a current sensor: 100 assumes 10 mV/A

// --- Handler aliases ---
// REMOVED: iftim_isr, iotim_dma_isr, usart1_isr, usart1_tx_dma_isr, usart2_isr, usart2_tx_dma_isr, tim1_com_isr.
// Vector handlers for those now live in config.c as IRQ-attributed wrappers (RISC-V needs the 'interrupt' attribute).
// Only these two renames stay: TIM3 is shared by throttle and hall, so config.c's TIM3_IRQHandler calls both.
// for Hall sensor
#ifdef HALL_MAP
#define tim3_isr tim3_hall_isr
#endif

/**
 * CH32V203C8U6 (QFN48X7, 7x7mm, 64KB Flash, 20KB SRAM, startup_ch32v20x_D6.S) Pinout & Resource Map:
 *
 * - TIM1 (3-Phase Complementary PWM, Default Mapping TIM1_RM = 00b):
 *   - CH1   - PA8  (Pin 29) - Phase A High
 *   - CH1N  - PB13 (Pin 26) - Phase A Low (default, no remap needed)
 *   - CH2   - PA9  (Pin 30) - Phase B High
 *   - CH2N  - PB14 (Pin 27) - Phase B Low (default, no remap needed)
 *   - CH3   - PA10 (Pin 31) - Phase C High
 *   - CH3N  - PB15 (Pin 28) - Phase C Low (default, no remap needed)
 *
 * - Internal OPAs (BEMF Comparators, enabled because PB13..PB15 handle TIM1_CHxN):
 *   - OPA1_CH1P - PB0  (Pin 18) - Phase A BEMF input
 *   - OPA2_CH1P - PA7  (Pin 17) - Phase B BEMF input
 *   - OPA1_CH1N - PA6  (Pin 16) - Phase C BEMF input (or shared/muxed via OPA_CTLR)
 *   - OPA1_CH0N - PB11 (Pin 22) - Virtual Neutral / Star Point for OPA1
 *   - OPA2_CH0N - PB10 (Pin 21) - Virtual Neutral / Star Point for OPA2
 *   - OPA1_OUT1 - PB1  (Pin 19) - OPA1 Comparator Out -> PCB trace to PA0 (TIM2_CH1)
 *   - OPA2_OUT1 - PA4  (Pin 14) - OPA2 Comparator Out -> PCB trace to PA1 (TIM2_CH2)
 *   (Note: PA2/OPA2_OUT0 and PA3/OPA1_OUT0 are left unused so PA2/PA3 stay free for USART2.)
 *
 * - TIM2 (Interface Timer: Zero-cross capture on CC1+CC2, internal OC/TRGO on CC3, Remap TIM2_RM = 10b):
 *   - CH1   - PA0  (Pin 10) - Input Capture 1 (wired on PCB from PB1 / OPA1_OUT1)
 *   - CH2   - PA1  (Pin 11) - Input Capture 2 (wired on PCB from PA4 / OPA2_OUT1)
 *   - CH3_2 - PB10 (Pin 21) - Internal Output Compare only (do NOT enable AF output on PB10)
 *
 * - TIM3 (Throttle / DShot Input-Output Timer, Partial Remap TIM3_RM = 10b):
 *   - CH1_2 - PB4  (Pin 40) - IOTIM Signal -> DMA1 CH6
 *   - CH2_2 - PB5  (Pin 41) - Spare / Optional Status LED
 *   - CH1   - PA6  (Pin 16) - Unused by TIM3 (used by OPA1_CH1N)
 *   - CH2   - PA7  (Pin 17) - Unused by TIM3 (used by OPA2_CH1P)
 *   - CH3   - PB0  (Pin 18) - Unused by TIM3 (used by OPA1_CH1P)
 *   - CH4   - PB1  (Pin 19) - Unused by TIM3 (used by OPA1_OUT1)
 *
 * - USART1 (Primary Telemetry / Config UART, Remap USART1_RM = 1):
 *   - TX    - PA9  (Pin 30) - Conflict with TIM1_CH2 (do not use)
 *   - RX    - PA10 (Pin 31) - Conflict with TIM1_CH3 (do not use)
 *   - TX_1  - PB6  (Pin 42) - Remapped TX -> DMA1 CH4
 *   - RX_1  - PB7  (Pin 43) - Remapped RX -> DMA1 CH5
 *
 * - USART2 (Secondary Serial UART, Default Mapping USART2_RM = 0):
 *   - TX    - PA2  (Pin 12) - Default TX -> DMA1 CH7
 *   - RX    - PA3  (Pin 13) - Default RX -> IRQ-driven (DMA1 CH6 reserved for TIM3_CH1)
 *
 * - ADC1 (Analog Sensing, DMA1 CH1):
 *   - ADC_IN5 - PA5 (Pin 15) - VBUS / Current Sense (additional ADC channels available on unused PA/PB pins)
 *
 * - Debug / Flashing (2-Wire SDI):
 *   - SWDIO - PA13 (Pin 34)
 *   - SWCLK - PA14 (Pin 37)
 *
 * - Note on USART3 / USART4:
 *   - USART3 (PB10/PB11) is not used because PB10/PB11 are assigned to OPA1_CH0N/OPA2_CH0N.
 *   - USART4 is only available on CH32V203RBx (D8) devices, not on CH32V203C8U6 (D6).
 */