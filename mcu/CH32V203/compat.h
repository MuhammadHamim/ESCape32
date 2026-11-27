// =============================================================================
// mcu/CH32V203/compat.h   (NEW FILE)
//
// WHY THIS FILE EXISTS
//   The portable ESCape32 code (io.c, main.c, telem.c, util.c) was written with
//   libopencm3 names such as TIM_CR1(IOTIM) or DMA1_CCR(6).  libopencm3 is ARM
//   only.  Instead of rewriting hundreds of lines, we keep those names and
//   translate each one to the WCH register name here.  One place to fix bugs.
//
//   Pattern:   libopencm3 name          ->  WCH name (struct field)
//              TIM_CR1(t)    "Control Register 1"     ->  t->CTLR1
//
// Included from common.h (CH32V203 branch), AFTER the WCH SDK headers.
// This first version covers io.c.  main.c / telem.c / util.c will need more
// lines (TIM1_*, USART1_*, FLASH_*, RCC_*);
// =============================================================================
#pragma once

#ifdef CH32V203

// The WCH header already defines plain constants with these names (bit masks, 0xFFFF / 0x00FF).
// Remove them so our function-like macros below (and defs.h's TIM_DTG) replace them cleanly.
// (Fixes the "'TIM_CNT' redefined" style warnings.)
#undef TIM_CNT
#undef TIM_PSC
#undef TIM_ARR
#undef TIM_CCR1
#undef TIM_CCR2
#undef TIM_DTG

// ---------------------------------------------------------------------------
// General timer registers.  `t` is a timer POINTER (TIM2, TIM3 ... are pointers
// in the WCH SDK).  All of them are 16 bit.
// ---------------------------------------------------------------------------
#define TIM_CR1(t) ((t)->CTLR1)      // Control Register 1: counter enable, auto-reload preload, update-event source
#define TIM_CR2(t) ((t)->CTLR2)      // Control Register 2: TRGO source, DMA-request selection
#define TIM_SMCR(t) ((t)->SMCFGR)    // Slave Mode Control: "reset the counter when a trigger arrives"
#define TIM_DIER(t) ((t)->DMAINTENR) // DMA/Interrupt Enable Register
#define TIM_SR(t) ((t)->INTFR)       // Status Register (interrupt flags). Write 0 to a bit to clear it
#define TIM_EGR(t) ((t)->SWEVGR)     // Event Generation Register: force an "update" etc. by software
#define TIM_CCMR1(t) ((t)->CHCTLR1)  // Capture/Compare Mode Register 1 (channel 1 and 2 modes + input filters)
#define TIM_CCMR2(t) ((t)->CHCTLR2)  // same for channel 3 and 4
#define TIM_CCER(t) ((t)->CCER)      // Capture/Compare Enable Register: channel on/off + edge polarity
#define TIM_CNT(t) ((t)->CNT)        // the counter itself
#define TIM_PSC(t) ((t)->PSC)        // Prescaler: counter clock = timer clock / (PSC + 1)
#define TIM_ARR(t) ((t)->ATRLR)      // Auto-Reload Register: counter restarts after this value
#define TIM_CCR1(t) ((t)->CH1CVR)    // Capture/Compare value of channel 1
#define TIM_CCR2(t) ((t)->CH2CVR)    // Capture/Compare value of channel 2
#define TIM_BDTR(t) ((t)->BDTR)      // Break and Dead-Time Register (ONLY advanced timer TIM1 has it!)

// io.c also uses the "TIM3_xxx" spelling directly in its command-line (CLI) code.
#define TIM3_CR1 TIM_CR1(TIM3)
#define TIM3_SMCR TIM_SMCR(TIM3)
#define TIM3_DIER TIM_DIER(TIM3)
#define TIM3_SR TIM_SR(TIM3)
#define TIM3_EGR TIM_EGR(TIM3)
#define TIM3_CCMR1 TIM_CCMR1(TIM3)
#define TIM3_CCER TIM_CCER(TIM3)
#define TIM3_PSC TIM_PSC(TIM3)
#define TIM3_ARR TIM_ARR(TIM3)
#define TIM3_CCR1 TIM_CCR1(TIM3)

// ---------------------------------------------------------------------------
// Bit values (identical to STM32 F1 / the WCH reference manual)
// ---------------------------------------------------------------------------
// CTLR1
#define TIM_CR1_CEN 0x0001              // Counter ENable
#define TIM_CR1_UDIS 0x0002             // Update DISable
#define TIM_CR1_URS 0x0004              // Update Request Source: only overflow makes an update interrupt
#define TIM_CR1_OPM 0x0008              // One-Pulse Mode: counter stops after one overflow
#define TIM_CR1_ARPE 0x0080             // Auto-Reload Preload Enable (new ARR takes effect at next overflow)
#define TIM_CR1_CKD_CK_INT_MUL_2 0x0100 // Clock Division: input-filter sampling clock = 2 x timer period (bits 9:8 = 01)
#define TIM_CR1_CKD_CK_INT_MUL_4 0x0200 // same, 4 x (bits 9:8 = 10)
// CTLR2
#define TIM_CR2_CCDS 0x0008 // Capture/Compare DMA Selection: DMA request on update event instead of CC event
#define TIM_CR2_TI1S 0x0080 // TI1 Selection (XOR of 3 pins), used for hall sensors
// SMCFGR
#define TIM_SMCR_SMS_RM 0x0004     // Slave Mode Select = Reset Mode
#define TIM_SMCR_TS_TI1FP1 0x0050  // Trigger Select = filtered TI1 (rising/falling chosen by CC1P)
#define TIM_SMCR_TS_TI1F_ED 0x0040 // Trigger Select = TI1 edge detector (ANY edge)
// DMAINTENR
#define TIM_DIER_UIE 0x0001   // Update Interrupt Enable
#define TIM_DIER_CC1IE 0x0002 // Capture/Compare 1 Interrupt Enable
#define TIM_DIER_CC2IE 0x0004
#define TIM_DIER_CC1DE 0x0200 // Capture/Compare 1 DMA request Enable
// INTFR
#define TIM_SR_UIF 0x0001
#define TIM_SR_CC1IF 0x0002
#define TIM_SR_CC2IF 0x0004
// SWEVGR
#define TIM_EGR_UG 0x0001 // Update Generation: reload registers + clear counter now
// CCER
#define TIM_CCER_CC1E 0x0001 // channel 1 enable
#define TIM_CCER_CC1P 0x0002 // channel 1 polarity (1 = falling edge / active low)
#define TIM_CCER_CC2E 0x0010
#define TIM_CCER_CC2P 0x0020
// CHCTLR1  (CCxS = what feeds the channel, ICxF = input filter, OCxPE/OCxM = output preload/mode)
#define TIM_CCMR1_CC1S_IN_TI1 0x0001
#define TIM_CCMR1_CC1S_IN_TRC 0x0003        // channel 1 fed by the trigger signal (TRC)
#define TIM_CCMR1_CC2S_IN_TI1 0x0200        // channel 2 fed by pin 1 (!!! 0x200, not 0x100)
#define TIM_CCMR1_IC1F_CK_INT_N_8 0x0030    // filter: 8 samples at timer clock
#define TIM_CCMR1_IC1F_DTF_DIV_8_N_8 0x0090 // filter: 8 samples at clock/8
#define TIM_CCMR1_IC2F_CK_INT_N_8 0x3000
#define TIM_CCMR1_IC2F_DTF_DIV_8_N_8 0x9000
#define TIM_CCMR1_OC1PE 0x0008     // output compare 1 preload enable
#define TIM_CCMR1_OC1M_PWM2 0x0070 // output compare 1 mode = PWM mode 2
// BDTR (not present on TIM3 -> io.c guards its use)
#define TIM_BDTR_MOE 0x8000

// ---------------------------------------------------------------------------
// DMA.  libopencm3 addresses a channel by number: DMA1_CCR(6).  WCH has one
// struct per channel (DMA_Channel_TypeDef) spaced 0x14 bytes apart, channel 1
// starting at DMA1_BASE + 0x08.
// ---------------------------------------------------------------------------
#define DMA_CH(n) ((DMA_Channel_TypeDef *)(DMA1_BASE + 0x08 + ((n) - 1) * 0x14))
#define DMA1_CCR(n) (DMA_CH(n)->CFGR)             // Channel Configuration (enable, direction, sizes, interrupts)
#define DMA1_CNDTR(n) (DMA_CH(n)->CNTR)           // Number of data items still to transfer
#define DMA1_CPAR(n) (DMA_CH(n)->PADDR)           // Peripheral address
#define DMA1_CMAR(n) (DMA_CH(n)->MADDR)           // Memory address
#define DMA1_IFCR (DMA1->INTFCR)                  // Interrupt Flag Clear Register (write 1 to clear)
#define DMA_IFCR_CTCIF(n) (2u << (((n) - 1) * 4)) // "Clear Transfer Complete Interrupt Flag" of channel n

#define DMA_CCR_EN 0x0001   // channel enable
#define DMA_CCR_TCIE 0x0002 // transfer-complete interrupt enable
#define DMA_CCR_DIR 0x0010  // 1 = memory -> peripheral, 0 = peripheral -> memory
#define DMA_CCR_CIRC 0x0020 // circular mode: start over when finished
#define DMA_CCR_MINC 0x0080 // memory address increments after each item
#define DMA_CCR_PSIZE_8BIT 0x0000
#define DMA_CCR_PSIZE_16BIT 0x0100
#define DMA_CCR_MSIZE_8BIT 0x0000
#define DMA_CCR_MSIZE_16BIT 0x0400

// ---------------------------------------------------------------------------
// Watchdogs and reset
// ---------------------------------------------------------------------------
#define IWDG_KR (IWDG->CTLR) // Independent watchdog "key" register
#define IWDG_KR_START 0xCCCC // writing this starts the watchdog (can never be stopped again)
#define IWDG_KR_RESET 0xAAAA // writing this "feeds the dog" (reloads the counter)
#define WWDG_CR (WWDG->CTLR) // Window watchdog control register
#define WWDG_CR_WDGA 0x0080  // activate; with counter bits = 0 this causes an IMMEDIATE reset (ESCape32 "reset now" trick)

#define scb_reset_system() NVIC_SystemReset() // RISC-V has no SCB; the PFIC does the system reset

// ===========================================================================
// ADDED FOR util.c  (resetcom, delayf, playmusic, playsound)  - also used by main.c
// ===========================================================================

// ---- TIM1 (advanced timer: the 3-phase bridge) -----------------------------
#define TIM1_CR1 TIM_CR1(TIM1)
#define TIM1_CR2 TIM_CR2(TIM1)
#define TIM1_DIER TIM_DIER(TIM1)
#define TIM1_SR TIM_SR(TIM1)
#define TIM1_EGR TIM_EGR(TIM1)
#define TIM1_CCMR1 TIM_CCMR1(TIM1)
#define TIM1_CCMR2 TIM_CCMR2(TIM1)
#define TIM1_CCER TIM_CCER(TIM1)
#define TIM1_PSC TIM_PSC(TIM1)
#define TIM1_ARR TIM_ARR(TIM1)
#define TIM1_CCR1 (TIM1->CH1CVR)
#define TIM1_CCR2 (TIM1->CH2CVR)
#define TIM1_CCR3 (TIM1->CH3CVR)
#define TIM1_CCR4 (TIM1->CH4CVR) // there is no TIM1_CCR5 on this chip: main.c then falls back to CCR4 by itself
#define TIM1_BDTR TIM_BDTR(TIM1)

// ---- "TIM6" ----------------------------------------------------------------
// ESCape32 uses a basic timer TIM6 as a stopwatch (arming wait, 1 s fault wait, sample clock of
// playsound).  CH32V203C8 has NO TIM6 (only TIM1..TIM4), so we borrow TIM4: nothing else uses it.
// Remember to enable its clock: RCC->APB1PCENR |= RCC_TIM4EN  (config.c, init()).
#define TIM6_CR1 TIM_CR1(TIM4)
#define TIM6_PSC TIM_PSC(TIM4)
#define TIM6_ARR TIM_ARR(TIM4)
#define TIM6_EGR TIM_EGR(TIM4)
#define TIM6_SR TIM_SR(TIM4)

// ---- more bit values ---------------------------------------------------------
// CHCTLR1: output compare modes.  OCxM = 3 bits: 100 force LOW, 101 force HIGH, 110 PWM1, 111 PWM2
#define TIM_CCMR1_OC1M_MASK 0x0070
#define TIM_CCMR1_OC1M_FORCE_LOW 0x0040
#define TIM_CCMR1_OC1M_FORCE_HIGH 0x0050
#define TIM_CCMR1_OC1M_PWM1 0x0060
#define TIM_CCMR1_OC2PE 0x0800
#define TIM_CCMR1_OC2M_MASK 0x7000
#define TIM_CCMR1_OC2M_FORCE_LOW 0x4000
#define TIM_CCMR1_OC2M_FORCE_HIGH 0x5000
#define TIM_CCMR1_OC2M_PWM1 0x6000
// CHCTLR2: channel 3 and 4
#define TIM_CCMR2_OC3PE 0x0008
#define TIM_CCMR2_OC3M_MASK 0x0070
#define TIM_CCMR2_OC3M_FORCE_LOW 0x0040
#define TIM_CCMR2_OC3M_FORCE_HIGH 0x0050
#define TIM_CCMR2_OC3M_PWM1 0x0060
#define TIM_CCMR2_OC4PE 0x0800
#define TIM_CCMR2_OC4M_PWM1 0x6000
// CCER: N = the complementary (low-side) output
#define TIM_CCER_CC1NE 0x0004
#define TIM_CCER_CC1NP 0x0008
#define TIM_CCER_CC2NE 0x0040
#define TIM_CCER_CC2NP 0x0080
#define TIM_CCER_CC3E 0x0100
#define TIM_CCER_CC3P 0x0200
#define TIM_CCER_CC3NE 0x0400
#define TIM_CCER_CC3NP 0x0800
#define TIM_CCER_CC4E 0x1000
// SWEVGR / INTFR / DMAINTENR
#define TIM_EGR_COMG 0x0020 // generate a COMmutation event now (copies the preloaded CCxE/CCxNE/OCxM bits)
#define TIM_EGR_BG 0x0080   // generate a Break: all outputs off (used by hard_fault_handler)
#define TIM_SR_COMIF 0x0020
#define TIM_DIER_COMIE 0x0020
// BDTR / CTLR2 (TIM1 only)
#define TIM_BDTR_OSSR 0x0800             // keep outputs driven (in "off state") when the channel is disabled
#define TIM_CR2_CCPC 0x0001              // Capture/Compare Preloaded Control: CCxE bits take effect only at COM event
#define TIM_CR2_CCUS 0x0004              // COM event may also be caused by a trigger from another timer
#define TIM_CR2_MMS_COMPARE_PULSE 0x0030 // TRGO = OC1 pulse

#endif // CH32V203