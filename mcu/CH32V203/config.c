// =============================================================================
// ESCape32 hardware layer for WCH CH32V203F8U6 (RISC-V core "QingKe V4B")
//
// WHAT THIS FILE IS FOR
//   ESCape32's main code (commutation, throttle decoding, telemetry...) is
//   chip-independent. Everything that touches chip-specific registers is
//   collected in a few "target" files like this one. The main code simply calls
//   the functions defined here:
//       init()      set up clocks/pins/ADC/DMA/interrupts at power-on
//       hsictl()    fine-tune the internal 8 MHz oscillator (used to calibrate it)
//       compctl()   choose which comparator watches which motor phase
//       io_serial() / io_analog()   re-purpose the throttle pin
//       adctrig()   start one ADC measurement run
//   plus interrupt handlers (the *_IRQHandler functions at the bottom).
//   Keep these NAMES identical to the G0 version, only the INSIDES change.
//
// STATUS
//   Done : GPIO, TIM1 pins, USART pins, ADC + DMA, HSI trim, interrupts
//   TODO : compctl() - comparator -> timer capture (see the function)
//   NOT ported: HSE start-up, WS2812 LED (needs TIM16, absent on CH32V203),
//               TIM15 throttle input (absent on CH32V203)
//
// CRASH COURSE: HOW A CH32 IS DIFFERENT FROM THE STM32G0 (read once!)
//   1. REGISTER NAMES. libopencm3 (used by ESCape32) names a register like
//      TIM1_CCMR1. WCH's header names the same thing TIM1->CHCTLR1. WCH gives
//      each peripheral a struct: TIM1->xxx, GPIOA->xxx, RCC->xxx, ADC1->xxx.
//      When you need a name, search ch32v20x.h (EVT/EXAM/SRC/Peripheral/inc).
//   2. GPIO. The G0 has MODER / AFRL / PUPDR (mode, alternate function, pull)
//      per pin. The CH32 has ONE 4-bit setting per pin in CFGLR (pins 0-7) and
//      CFGHR (pins 8-15). It can't pick "alternate function number 2" on a pin.
//      Instead each peripheral signal has a DEFAULT pin, and a few can be moved
//      with REMAP bits in AFIO->PCFR1.
//   3. DMA. The G0 has a DMAMUX: you may connect any peripheral request to any
//      DMA channel. The CH32 has NO DMAMUX: each request is WIRED to one channel
//      (ADC1 -> ch1, TIM3_CH1 -> ch6, USART1_TX -> ch4 ...). The channel numbers
//      in config.h come from that table.
//   4. ADC. G0: pick channels with a bit-mask (CHSELR). CH32: write the channel
//      number of each conversion slot into a list (RSQR1..3), like STM32F1.
//   5. INTERRUPTS. G0 (ARM): NVIC + vector names like TIM3_IRQHandler. CH32
//      (RISC-V): controller is called PFIC, but WCH kept the NVIC_xxx function
//      names. Vector names come from the startup file (startup_ch32v20x_D6.S).
//      A handler whose name is NOT defined anywhere falls into a weak default
//      handler = infinite loop. So only enable interrupts you have a handler for!
//   6. No SYSCFG, no DMAMUX, no TIM6/15/16/17, no comparator registers in the
//      public SDK, no VTOR register (the vector table is set up by startup code).
// =============================================================================

#include "ch32v20x.h" // WCH register definitions: RCC, GPIOx, TIMx, ADC1, DMA1, AFIO...
#include "config.h"	  // ESCape32 per-board settings (timer names, DMA channels, CLK)
#include "defs.h"	  // ESCape32 common definitions (SENS_CNT, IO_ANALOG, ...)
#include "common.h"	  // ESCape32 helpers: clamp(), adcdata() ...

// These two functions are written in OTHER files (io.c = throttle input,
// hall.c = hall sensors). We only DECLARE them here (a "prototype") so the
// compiler knows they exist when TIM3_IRQHandler() calls them at the bottom.
//
// WHY ARE THEY CALLED FROM HERE? One timer = one interrupt vector = ONE handler
// function. TIM3 is used by BOTH the throttle (channel 1) and the hall sensor
// (when enabled), so one TIM3_IRQHandler() must call both pieces of code. That is
// the only reason these two lines exist.
// Could we use another timer? YES, TIM4 is free (see the notes at the end of this file).
void tim3_io_isr(void);
#ifdef HALL_MAP
void tim3_hall_isr(void); // only built if the board uses hall sensors
#endif

// -----------------------------------------------------------------------------
// BOARD SETTINGS (normally in config.h / defs.h; defaults here so it compiles)
// -----------------------------------------------------------------------------
// Which ADC channel is which measurement. ESCape32 reads (in this order):
//   current, voltage, "u" (extra, e.g. temperature/other) -> SENS_CNT of them (1..3).
// ADC channel number = ADC_INx of the pin: PA0..PA7 = IN0..IN7, PB0 = IN8,
// PB1 = IN9, PC0..PC5 = IN10..IN15. CHECK YOUR 20-PIN PACKAGE FOR WHICH EXIST.
#ifndef SENS_CH_LIST
#define SENS_CH_LIST 4, 5, 6 // example: PA4 = current, PA5 = voltage, PA6 = "u"
#endif
// ADC channel used when the throttle is an ANALOG voltage (PWM-free "pot" input).
// Our throttle pin is PA6 = ADC_IN6.
#ifndef ANALOG_CHAN
#define ANALOG_CHAN 6
#endif

// The chip has 2 internal "virtual" ADC channels:
#define ADC_CH_TEMP 16	// internal temperature sensor
#define ADC_CH_VREF 17	// internal fixed reference voltage (~1.2 V), used to measure VDD
#define VREFINT_MV 1200 // nominal value of that reference in millivolts (CHECK datasheet)

// TEMPERATURE CONVERSION
// The G0 stored 2 factory calibration points (30C and 130C). CH32 stores ONE
// point at flash address 0x1FFFF720 (value found in WCH's ch32v20x_adc.c):
//   low 16 bits  = sensor voltage in mV at the reference temperature
//   high 16 bits = that reference temperature in degrees C
// VERIFY this address for the F8U6 in the reference manual.
#define TS_CAL (*(volatile uint32_t *)0x1FFFF720)
// Sensor voltage falls ~4.3 mV per degree C. So:  T = Tref - (mV - Vref_mV) * 10 / 43
// (+21 = half of 43 = rounding). ESCape32 wants QUARTER degrees, hence "* 4".
#define TEMP_FUNC(mv) \
	((((int)(TS_CAL >> 16) & 0xffff) - (((mv) - (int)(TS_CAL & 0xffff)) * 10 + 21) / 43) * 4)

// GPIO PIN MODE CODES. One pin = 4 bits = CNF[1:0] (upper 2) + MODE[1:0] (lower 2).
//   MODE=00 -> input;  MODE=11 -> output, 50 MHz.
//   Input  CNF: 00 analog, 01 floating, 10 pull-up or pull-down
//   Output CNF: 00 push-pull, 01 open-drain, 10 ALTERNATE-function push-pull, 11 alt. open-drain
// "Alternate function" = the pin is driven by a peripheral (timer, USART) not by software.
#define PIN_ANALOG 0x0	 // CNF=00 MODE=00: analog input (also the lowest-power state)
#define PIN_FLOAT_IN 0x4 // CNF=01 MODE=00: digital input, no pull resistor
#define PIN_PULL_IN 0x8	 // CNF=10 MODE=00: digital input with pull-up OR pull-down
#define PIN_OUT_PP 0x3	 // CNF=00 MODE=11: normal output, push-pull (unused here)
#define PIN_AF_PP 0xb	 // CNF=10 MODE=11: peripheral-driven output, push-pull

// File-local (static) variables.
static uint8_t len, ain; // len = number of ADC conversions per run; ain = 1 if analog throttle is included
static uint16_t buf[6];	 // DMA drops the ADC results here (max 3 sensors + analog + temp + vref = 6)

// DMA1->INTFR / INTFCR use 4 bits per channel: bit0 global, bit1 TRANSFER-COMPLETE,
// bit2 half, bit3 error. This macro gives the "transfer complete" bit of channel n (1..7).
#define DMA_TCIF(n) (2u << (((n) - 1) * 4))

// INTERRUPT HANDLER ATTRIBUTE. On RISC-V a handler can't just be a normal C
// function: the CPU must save/restore registers and return with "mret".
// "interrupt" tells GCC to generate that. WCH's own compiler (MounRiver) also
// offers "WCH-Interrupt-fast": the HARDWARE saves registers (HPE feature, switched on
// by the "csrw 0x804" line in the startup file) which is faster.
// If you use mainline GCC, build with -DIRQ='__attribute__((interrupt))'.
#ifndef IRQ
#define IRQ __attribute__((interrupt("WCH-Interrupt-fast")))
#endif

// -----------------------------------------------------------------------------
// HELPERS
// -----------------------------------------------------------------------------

// Set the 4-bit mode of ONE pin. g = GPIOA/GPIOB/GPIOC, n = pin number 0..15.
static void pin(GPIO_TypeDef *g, int n, uint32_t mode)
{
	// pins 0-7 live in CFGLR, pins 8-15 in CFGHR (volatile = real hardware register)
	volatile uint32_t *r = n < 8 ? &g->CFGLR : &g->CFGHR;
	int s = (n & 7) * 4;					// bit position of this pin's 4-bit field
	*r = (*r & ~(0xfu << s)) | (mode << s); // clear the 4 bits, then write the new mode
}

// Digital input WITH PULL-UP (the throttle line idles high).
static void pullup(GPIO_TypeDef *g, int n)
{
	pin(g, n, PIN_PULL_IN); // "input with pull" ...
	g->BSHR = 1u << n;		// ... and writing 1 to that pin's output bit selects PULL-UP
							// (BSHR = bit-set register; BCR would clear it = pull-down)
}

// Put channel number "ch" into conversion slot "idx" of the ADC's regular sequence.
// Slots 0-5 are in RSQR3, 6-11 in RSQR2, 12-15 in RSQR1, 5 bits per slot.
// (The G0 did this with the CHSELR bit-mask / nibble list.)
static void sq(int idx, int ch)
{
	volatile uint32_t *r;
	int s;
	if (idx < 6)
		r = &ADC1->RSQR3, s = idx * 5;
	else if (idx < 12)
		r = &ADC1->RSQR2, s = (idx - 6) * 5;
	else
		r = &ADC1->RSQR1, s = (idx - 12) * 5;
	*r = (*r & ~(0x1fu << s)) | ((uint32_t)ch << s);
}

// -----------------------------------------------------------------------------
// init(): called once at start-up. SystemInit() (in system_ch32v20x.c, with
// SYSCLK_FREQ_96MHz_HSI defined) has ALREADY set the 96 MHz clock before this runs,
// so unlike the G0 version there is no clock code here.
// Bus clocks from that file: HCLK = PCLK2 = 96 MHz, PCLK1 = 48 MHz. Timers on
// the PCLK1 bus (TIM2/3/4) are fed 2 x PCLK1 = 96 MHz when PCLK1 is divided,
// so every timer should count at 96 MHz = "CLK" in config.h. VERIFY once with a scope.
// -----------------------------------------------------------------------------
void init(void)
{
	// ADC clock = PCLK2 / divider. 96 / 8 = 12 MHz (the ADC must stay <= ~14 MHz).
	RCC->CFGR0 = (RCC->CFGR0 & ~RCC_ADCPRE) | RCC_ADCPRE_DIV8;

	// CLOCK ENABLE: a peripheral does nothing (and its registers ignore writes)
	// until its clock is switched on. G0: RCC_IOPENR / AHBENR / APBENR1 / APBENR2.
	// CH32: AHBPCENR (DMA), APB2PCENR (fast bus), APB1PCENR (slower bus).
	RCC->AHBPCENR |= RCC_DMA1EN;
	// APB2 bus: AFIO (pin remap), GPIO ports A/B/C, ADC1, TIM1, USART1
	RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPAEN | RCC_IOPBEN | RCC_IOPCEN | RCC_ADC1EN | RCC_TIM1EN | RCC_USART1EN;
	// APB1 bus: TIM2 (our IFTIM), TIM3 (throttle input), TIM4 (unused), window watchdog (ESCape32 uses it as a watchdog)
	RCC->APB1PCENR |= RCC_TIM2EN | RCC_TIM3EN | RCC_WWDGEN;
	// (The G0 code also reset every peripheral first. After a power-up reset this is already
	// the state, so it is left out.)

	// --- MOTOR OUTPUT PINS (TIM1 makes the 3 half-bridge PWM signals) -------------
	// Default TIM1 mapping (no remap): CH1 PA8, CH2 PA9, CH3 PA10 = HIGH-side signals,
	// CH1N_1 PA7, CH2N_1 PB0, CH3N_1 PB1 = LOW-side signals(Remapping functions)

	// Check B13..B15 exist on a 20-pin package!
	// USART1's default pins are PA9/PA10, which TIM1 now uses, so move USART1 to PB6/PB7:
	AFIO->PCFR1 |= AFIO_PCFR1_USART1_REMAP;
	pin(GPIOA, 8, PIN_AF_PP);	 // TIM1_CH1(PA8->14)  (driven by the timer, not by software)
	pin(GPIOA, 9, PIN_AF_PP);	 // TIM1_CH2(PA9->15)
	pin(GPIOA, 10, PIN_AF_PP);	 // TIM1_CH3(PA10->18)
	pin(GPIOA, 7, PIN_AF_PP);	 // TIM1_CH1N_1(PA7->7)
	pin(GPIOB, 0, PIN_AF_PP);	 // TIM1_CH2N_1(PB0->8)
	pin(GPIOB, 1, PIN_AF_PP);	 // TIM1_CH3N_1(PB1->9)
	pin(GPIOB, 6, PIN_AF_PP);	 // USART1_TX (remapped). ESCape32 uses it half-duplex / single wire
	pin(GPIOB, 7, PIN_FLOAT_IN); // USART1_RX (remapped)
	GPIOB->BSHR = 1u << 6;		 // pull-up bit (only relevant when the pin is in an input mode)

	// --- TIM3: throttle input (+ hall sensor if the board has one) -----------------
	// No remap: TIM3_CH1 = PA6, TIM3_CH2 = PA7, TIM3_CH3 = PB0, TIM3_CH4 = PB1.
	RCC->APB1PCENR |= RCC_TIM3EN;
#ifdef HALL_MAP
	pullup(GPIOA, 6); // hall sensor on TIM3_CH1
#endif
#ifndef ANALOG
	pullup(GPIOA, 6); // PWM throttle on TIM3_CH1, idle high
#endif

#ifdef CMP_OUT_PINS
	// Only if the comparator outputs can reach the timer ONLY through pins:
	// wire them to TIM2_CH1 (PA0) and TIM2_CH2 (PA1) = the capture inputs of IFTIM.
	pin(GPIOA, 0, PIN_FLOAT_IN);
	pin(GPIOA, 1, PIN_FLOAT_IN);
#endif

	// --- INTERRUPT PRIORITIES (smaller number = more urgent; WCH uses the top bits) ---
	NVIC_SetPriority(TIM3_IRQn, 0x40);			// throttle / hall edges: urgent
	NVIC_SetPriority(USART1_IRQn, 0x80);		// telemetry: can wait
	NVIC_SetPriority(DMA1_Channel6_IRQn, 0x40); // throttle capture finished
	NVIC_SetPriority(DMA1_Channel4_IRQn, 0x80); // USART1 transmit finished
	NVIC_SetPriority(DMA1_Channel1_IRQn, 0x80); // ADC results ready

	// --- ENABLE THE INTERRUPTS WE HAVE HANDLERS FOR ---
	// IMPORTANT: enabling an interrupt that has no handler = CPU hangs in the weak
	// default handler when it fires. Match this list to your handlers (see config.h).
	NVIC_EnableIRQ(TIM1_TRG_COM_IRQn); // TIM1 commutation event = tim1_com_isr (config.h)
	NVIC_EnableIRQ(TIM2_IRQn);		   // IFTIM zero-cross captures = iftim_isr
	NVIC_EnableIRQ(TIM3_IRQn);		   // throttle + hall (handler at the bottom of this file)
	NVIC_EnableIRQ(USART1_IRQn);
	NVIC_EnableIRQ(DMA1_Channel1_IRQn); // ADC (handler at the bottom of this file)
	NVIC_EnableIRQ(DMA1_Channel6_IRQn); // throttle capture = iotim_dma_isr
	NVIC_EnableIRQ(DMA1_Channel4_IRQn); // USART1 TX        = usart1_tx_dma_isr
	// DMA REQUEST ROUTING. The G0 used DMAMUX1_CxCR(...) here. On CH32 there is NOTHING to
	// write: the request is hard-wired to the channel (verify in the RM DMA table):
	//   ch1 ADC1 | ch4 USART1_TX | ch5 USART1_RX | ch6 TIM3_CH1

	// --- IFTIM = TIM2: the "commutation timer" -----------------------------------
	// It measures the time between zero-crossings (comparator edges) and also fires the
	// next commutation. TIM1 (the PWM timer) is made a SLAVE of TIM2:
	TIM1->SMCFGR = TIM_TS_0;										  // TIM1 trigger input = ITR1 (= TIM2's TRGO). VERIFY
																	  // "internal trigger" table in the RM.
	TIM2->CTLR2 = TIM_MMS_2 | TIM_MMS_1;							  // TIM2 master mode = 110: send "OC3REF" as TRGO
	TIM2->CHCTLR1 = TIM_CC1S_0 | TIM_CC2S_0;						  // TIM2 channels 1 and 2 = INPUT capture (TI1, TI2).
																	  // The input FILTER bits are set later by ESCape32
																	  // using IFTIM_ICM1/2/3 from config.h.
	TIM2->CHCTLR2 = TIM_OC3PE | TIM_OC3M_2 | TIM_OC3M_1 | TIM_OC3M_0; // channel 3 = output compare,
																	  // PWM mode 2, preload on (this is the "timer for
																	  // the next commutation", IFTIM_OCR = CH3CVR)
	TIM2->CCER = TIM_CC1E | TIM_CC2E;								  // enable capture on channel 1 and 2 (rising edge)

	// --- ADC1: read current/voltage/temperature in the background -------------------
	ADC1->CTLR1 = ADC_SCAN;					// SCAN mode: one start converts the WHOLE list
	ADC1->CTLR2 = ADC_ADON					// ADC power on
				  | ADC_TSVREFE				// enable internal temp sensor + Vref channels
				  | ADC_EXTSEL				// trigger source = 111 = SOFTWARE start
				  | ADC_EXTTRIG				// allow that trigger source (needed for SWSTART)
				  | ADC_DMA;				// after each conversion send result to DMA
	for (volatile int i = 0; i < 2000; i++) // wait for the ADC to wake up (a few microseconds)
		;
	ADC1->CTLR2 |= ADC_RSTCAL;		 // reset calibration ...
	while (ADC1->CTLR2 & ADC_RSTCAL) // ... hardware clears the bit when finished
		;
	ADC1->CTLR2 |= ADC_CAL; // run calibration (removes ADC offset error)
	while (ADC1->CTLR2 & ADC_CAL)
		;

	// SAMPLE TIME per channel (3 bits, same value for every channel):
	//   0=1.5  1=7.5  2=13.5  3=28.5  4=41.5  5=55.5  6=71.5  7=239.5 ADC-clock cycles.
	// 71.5 + 12.5 (conversion) = 84 cycles @ 12 MHz = 7 us per channel. Longer = more
	// accurate for high-impedance dividers; shorter = faster. Tune later.
	{
		uint32_t s1 = 0, s2 = 0;
		for (int c = 0; c < 10; c++)
			s2 |= 6u << (c * 3); // channels 0..9  in SAMPTR2
		for (int c = 0; c < 8; c++)
			s1 |= 6u << (c * 3); // channels 10..17 in SAMPTR1
		ADC1->SAMPTR2 = s2;
		ADC1->SAMPTR1 = s1;
	}

	// Build the conversion list: [sensors...] [analog throttle?] [temperature] [vref]
	static const uint8_t sens[] = {SENS_CH_LIST};
	len = 0;
	for (int i = 0; i < SENS_CNT; i++)
		sq(len++, sens[i]);
	if (IO_ANALOG)
	{
		sq(len++, ANALOG_CHAN);
		ain = 1; // remember the analog value is in the list (see the ADC handler)
	}
	sq(len++, ADC_CH_TEMP);
	sq(len++, ADC_CH_VREF);
	// tell the ADC how long the list is: field L (bits 23:20) = length - 1
	ADC1->RSQR1 = (ADC1->RSQR1 & ~ADC_L) | ((uint32_t)(len - 1) << 20);

	// DMA channel 1 is fixed to the ADC: it copies ADC1->RDATAR into buf[] one by one.
	DMA1_Channel1->PADDR = (uint32_t)&ADC1->RDATAR; // PADDR = "peripheral address" (source)
	DMA1_Channel1->MADDR = (uint32_t)buf;			// MADDR = "memory address" (destination)
}

// -----------------------------------------------------------------------------
// hsictl(): nudge the internal 8 MHz oscillator (HSI) up/down by x steps.
// ESCape32 uses this to calibrate the clock against the throttle signal timing.
// G0: 7-bit trim in RCC_ICSCR. CH32: 5-bit trim (HSITRIM) in RCC->CTLR bits 7:3,
// so the range is 0..31 instead of 0..127 and each step is a bigger frequency change.
// -----------------------------------------------------------------------------
void hsictl(int x)
{
	uint32_t cr = RCC->CTLR;
	int tv = (cr & RCC_HSITRIM) >> 3; // current trim value
	RCC->CTLR = (cr & ~RCC_HSITRIM) | (uint32_t)clamp(tv + x, 0, 0x1f) << 3; // write new, limited
}

// -----------------------------------------------------------------------------
// compctl(x): select which motor phase the comparator watches (x & 3: which input,
// x & 4: invert polarity). On the G0 the comparator output was connected INSIDE the
// chip to TIM2 (via TIM2_TISEL) so the timer could capture the zero-crossing time.
// !!! NOT PORTED YET !!! The public CH32V20x SDK only has OPA->CR (op-amp enable +
// input/output select). Read the OPA/CMP chapter of the reference manual and find out:
//   1) which pins are the comparator inputs (connect them to the phase voltages),
//   2) whether the comparator output can go INTO a timer channel, or only out to a pin
//      (then wire it to PA0/PA1 = TIM2_CH1/CH2 and build with -DCMP_OUT_PINS),
//   3) which bit inverts the polarity.
// Your senior's AM32 port in mcu/v203/drivers already solved this on the same chip.
// -----------------------------------------------------------------------------
void compctl(int x)
{
	(void)x;
}

// -----------------------------------------------------------------------------
// io_serial(): the throttle wire switches from "PWM/DShot timer capture" to
// "serial protocol" (when ESCape32 detects serial data).
// -----------------------------------------------------------------------------
void io_serial(void)
{
	// Stop TIM3 channel 1 (throttle) but keep TIM3 running: hall may use it.
	TIM3->DMAINTENR &= ~(TIM_CC1DE | TIM_CC1IE); // no DMA request / no interrupt from CH1
	TIM3->CCER &= ~TIM_CC1E;					 // channel 1 capture off
	DMA1_Channel6->CFGR = 0;					 // stop the throttle DMA channel
	NVIC_ClearPendingIRQ(TIM3_IRQn);			 // forget an interrupt that was already waiting
	RCC->APB1PCENR |= RCC_USART2EN;				 // clock for USART2
	pin(GPIOA, 2, PIN_AF_PP);					 // USART2_TX on PA2 (default mapping)
#ifdef IO_RXTX
	pin(GPIOA, 3, PIN_FLOAT_IN); // USART2_RX on PA3 (only if the board has separate RX)
#endif
}

// -----------------------------------------------------------------------------
// io_analog(): the throttle wire becomes an analog voltage input read by the ADC.
// -----------------------------------------------------------------------------
void io_analog(void)
{
	TIM3->DMAINTENR &= ~(TIM_CC1DE | TIM_CC1IE); // stop timer capture on the throttle channel
	TIM3->CCER &= ~TIM_CC1E;
	DMA1_Channel6->CFGR = 0;
	NVIC_ClearPendingIRQ(TIM3_IRQn);
	pin(GPIOA, 6, PIN_ANALOG); // PA6 -> analog mode = ADC_IN6 (ANALOG_CHAN)
}

// -----------------------------------------------------------------------------
// adctrig(): start ONE run of the ADC list. Called regularly from ESCape32's main loop.
// -----------------------------------------------------------------------------
void adctrig(void)
{
	if (DMA1_Channel1->CFGR & DMA_CFGR1_EN) // previous run still going? then skip
		return;
	DMA1_Channel1->CNTR = len;				   // number of 16-bit values the DMA must move
	DMA1_Channel1->CFGR = DMA_CFGR1_EN		   // channel on
						  | DMA_CFGR1_TCIE	   // interrupt when all values are copied
						  | DMA_CFGR1_MINC	   // destination address +1 value after each copy (fills buf[])
						  | DMA_CFGR1_PSIZE_0  // peripheral side = 16 bit
						  | DMA_CFGR1_MSIZE_0; // memory side = 16 bit (DIR bit = 0: peripheral -> memory)
	ADC1->CTLR2 |= ADC_SWSTART;				   // start converting the whole list now
}

// -----------------------------------------------------------------------------
// INTERRUPT HANDLERS. Names must equal the names in startup_ch32v20x_D6.S.
// -----------------------------------------------------------------------------

// ADC finished the whole list and DMA copied it into buf[] -> process the numbers.
// (G0: dma1_channel4_7_dmamux_isr; CH32 gives every DMA channel its own vector.)
IRQ void DMA1_Channel1_IRQHandler(void)
{
	DMA1->INTFCR = DMA_TCIF(1); // clear the "transfer complete" flag (else it fires forever)
	DMA1_Channel1->CFGR = 0;	// switch the channel off until adctrig() arms it again
	int i = 0, u = 0, v = 0, c = 0, a = 0;
	// buf[] holds the values in the same order as the list built in init():
#if SENS_CNT >= 2
	c = buf[i++]; // current
#endif
#if SENS_CNT >= 1
	v = buf[i++]; // voltage
#endif
#if SENS_CNT >= 3
	u = buf[i++]; // extra sensor
#endif
	if (ain)
		a = buf[i++]; // analog throttle
	// buf[i] = temperature sensor raw value, buf[i+1] = internal 1.2 V reference raw value.
	// If VDD is 3.3 V the reference reads 1200/3300*4096 ~= 1489; if VDD sags it reads higher.
	// So r = VDD in millivolts. raw * r >> 12 = millivolts for ANY raw ADC value (">> 12" = "/ 4096").
	int r = VREFINT_MV * 4096 / buf[i + 1];
	adcdata(TEMP_FUNC(buf[i] * r >> 12), u * r >> 12, v * r >> 12, c * r >> 12, a * r >> 12);
}

// TIM3 interrupt: throttle capture and/or hall sensor. One vector -> call both users.
IRQ void TIM3_IRQHandler(void)
{
	tim3_io_isr(); // defined in the throttle code (config.h: iotim_isr)
#ifdef HALL_MAP
	tim3_hall_isr(); // defined in the hall code (config.h: tim3_isr)
#endif
}

// =============================================================================
// NOTE: giving hall its own timer (TIM4)?  Yes, it is possible and cleaner.
//   + No shared handler: TIM4_IRQHandler would belong to hall alone, and
//     TIM3_IRQHandler would call only tim3_io_isr().
//   - hall.c uses TIM3 registers today, so you must rewrite them for TIM4
//     (enable RCC APB1 TIM4, NVIC TIM4_IRQn, change the pin).
//   - TIM4 default pins are CH1 PB6, CH2 PB7, CH3 PB8, CH4 PB9. PB6/PB7 are the
//     remapped USART1 pins here, so hall would use PB8 or PB9.
//   - TIM4 DMA requests: CH1 -> DMA ch1 (the ADC's), CH2 -> ch4 (USART1_TX),
//     CH3 -> ch5 (USART1_RX). Plain interrupts are fine; just don't use DMA for them.
//   - Check the datasheet that the F8U6 package really has TIM4 and those pins.
// Advice: keep TIM3 shared for the FIRST working port (fewer changes to upstream
// ESCape32), move hall to TIM4 later if you want.
// =============================================================================