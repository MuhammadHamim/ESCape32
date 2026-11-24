#pragma once

#define CLK 96000000

// --- Interface timer: TIM2 (commutation / zero-cross capture on CC1+CC2, CC3 = OC/TRGO) ---
// NOTE: G0 TIM2 is 32-bit, CH32V203 TIM2 is 16-bit. Check IFTIM_XRES and counter-width use in the common code.
#define IFTIM TIM2
#define IFTIM_XRES 2
#define IFTIM_ICFL 64
#define IFTIM_ICMR (TIM2->CHCTLR1)
// IC filter codes (CHCTLR1 IC1F = bits 7:4, IC2F = bits 15:12): 0x9 = DTF/8 N=8, 0x7 = DTF/4 N=8, 0x5 = DTF/2 N=8
#define IFTIM_ICM1 (TIM_CC1S_0 | (0x9 << 4) | TIM_CC2S_0 | (0x9 << 12))
#define IFTIM_ICM2 (TIM_CC1S_0 | (0x7 << 4) | TIM_CC2S_0 | (0x7 << 12))
#define IFTIM_ICM3 (TIM_CC1S_0 | (0x5 << 4) | TIM_CC2S_0 | (0x5 << 12))
#define IFTIM_ICIE (TIM_CC1IE | TIM_CC2IE)
#define IFTIM_ICR (sr & TIM_CC1IF ? TIM2->CH1CVR : TIM2->CH2CVR)
#define IFTIM_OCR (TIM2->CH3CVR)
#define iftim_isr TIM2_IRQHandler

// --- IO (throttle) timer: TIM3 CH1 on PA6 -------------------------------
#define IOTIM TIM3
// TIM3_CH3/PB0 conflicts with TIM1_CH2N/PB0, so throttle capture uses CH1 PA6.
#define IOTIM_IDR (GPIOA->INDR & (1u << 6)) // PA6
#define iotim_isr tim3_io_isr
// TIM3_CH1 generates the throttle DMA request on DMA1 channel 6.
#define IOTIM_DMA 6 // TIM3_CH1 -> DMA1 channel 6
#define iotim_dma_isr DMA1_Channel6_IRQHandler

// --- USART DMA ---
#define USART1_TX_DMA 4 // PB6 (remapped) -> DMA1 CH4
#define USART1_RX_DMA 5 // PB7 (remapped) -> DMA1 CH5
#define usart1_isr USART1_IRQHandler
#define usart1_tx_dma_isr DMA1_Channel4_IRQHandler

// --- Handler aliases ---
#define tim1_com_isr TIM1_TRG_COM_IRQHandler
// for Hall sensor
#define tim3_isr tim3_hall_isr

/**
 * CH32V203K8T6 pinout:
 * TIM1 is used for PWM output on CH1-CH3
 *  - CH1 - PA8
 *  - CH1N_1 - PA7(remapping function)
 *  - CH2 - PA9
 *  - CH2N_1 - PB0 (remapping function)
 *  - CH3 - PA10
 *  - CH3N_1 - PB1 (remapping function)
 *
 * TIM2 is used for commutation and zero-cross capture on CC1+CC2, with CC3 as OC/TRGO.
 *  - CH1 - PA0
 *  - CH2 - PA1
 *  - CH3 - PA2
 * TIM3 is used for throttle input capture on CH1 with DMA1 channel 6
 *  - CH1 - PA6 - DMA1 CH6
 *  -CH2 - PA7 - N/A
 *  -CH3 - PB0 - DMA1 CH2 (conflict with TIM1_CH2N_1 gpio)
 *  -CH4 - PB1 - DMA1 CH3 (conflict with TIM1_CH3N_1 gpio)
 *
 * USART1 is used for communication with DMA1 channels 4 and 5 for TX and RX respectively.
 *  - TX - PA9 - DMA1 CH4 (conflict with TIM1_CH2 gpio)
 *  - RX - PA10 - DMA1 CH5 (conflict with TIM1_CH3 gpio)
 *  - TX_1 - PB6 (remapping function)
 *  - RX_1 - PB7 (remapping function)
 *
 * USART2 is used for communication with DMA1 channels 2 and 3 for TX and RX respectively.
 *  - TX - PA2 - DMA1 CH7 (conflict with TIM2_CH3 gpio)
 *  - RX - PA3 - DMA1 CH6
 *
 * USART3 is used for communication with DMA1 channels 6 and 7 for TX and RX respectively.
 *  - TX - PB10 - DMA1 CH2 (conflict with TIM1_CH3 GPIO)
 *  - RX - PB11 - DMA1 CH3
 *
 * USART4 is used for communication with DMA1 channels 6 and 7 for TX and RX respectively.
 *  - TX - PB0 - DMA2 CH5 (conflict with TIM1_CH2N_1 GPIO)
 *  - RX - PB1 - DMA2 CH3 (conflict with TIM1_CH3N_1 GPIO)
 *  - TX_1 - PA5 (remapping function)
 *  - RX_1 - PB5 (remapping function)
 */