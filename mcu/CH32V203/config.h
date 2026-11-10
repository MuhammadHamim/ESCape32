

#pragma once

#define CLK 96000000

// --- Throttle Input Timer ---
// Using TIM2, Channel 1 for input capture
#define IFTIM TIM2
#define IFTIM_XRES 2
#define IFTIM_ICFL 64
#define IFTIM_ICMR TIM2_CCMR1
//configure channel 
#define IFTIM_ICM1 (TIM_CCMR1_CC1S_IN_TI1 | TIM_CCMR1_IC1F_DTF_DIV_8_N_8 | TIM_CCMR1_CC2S_IN_TI2 | TIM_CCMR1_IC2F_DTF_DIV_8_N_8)
#define IFTIM_ICM2 (TIM_CCMR1_CC1S_IN_TI1 | TIM_CCMR1_IC1F_DTF_DIV_4_N_8 | TIM_CCMR1_CC2S_IN_TI2 | TIM_CCMR1_IC2F_DTF_DIV_4_N_8)
#define IFTIM_ICM3 (TIM_CCMR1_CC1S_IN_TI1 | TIM_CCMR1_IC1F_DTF_DIV_2_N_8 | TIM_CCMR1_CC2S_IN_TI2 | TIM_CCMR1_IC2F_DTF_DIV_2_N_8)
#define IFTIM_ICIE (TIM_DIER_CC1IE | TIM_DIER_CC2IE)
#define IFTIM_ICR (sr & TIM_SR_CC1IF ? TIM2_CCR1 : TIM2_CCR2)

#define IFTIM_OCR TIM2_CCR3
#define iftim_isr TIM2_IRQHandler

// --- IO Timer ---
// Using TIM3 channel 3(PB0) for auxiliary IO tasks
#define IOTIM TIM3
#define IOTIM_IDR (GPIOB->INDR & (1u << 0)) // PB0

#define iotim_isr tim3_io_isr


#define IOTIM_DMA 2
#define iotim_dma_isr DMA1_Channel2_IRQHandler

// #ifndef IO_AUX
// #elif IO_AUX == 0xB8
// #define IOTIM2 TIM16
// #define iotim2_isr tim16_fdcan_it0_isr
// #elif IO_AUX == 0xB9
// #define IOTIM2 TIM17
// #define iotim2_isr tim17_fdcan_it1_isr
// #else
// #define IOTIM2 TIM3
// #define iotim2_isr tim34_isr
// #endif

// USART DMA channels
// USART1: TX=4, RX=5
#define USART1_TX_DMA 4   // Channel 4
#define USART1_RX_DMA 5   // Channel 5  
#define usart1_tx_dma_isr DMA1_Channel4_IRQHandler

#define USART2_TX_DMA 7   // Channel 7
#define USART2_RX_DMA 6   // Channel 6
#define usart2_isr USART2_IRQHandler


// --- Interrupt Handler Aliases ---
#define tim1_com_isr TIM1_TRG_COM_IRQHandler
#define tim3_isr tim3_hall_isr
