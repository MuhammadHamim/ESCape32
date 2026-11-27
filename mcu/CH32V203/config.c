// =============================================================================
// ESCape32 hardware layer for WCH CH32V203C8U6 (QingKe V4B, RISC-V)
// Location in repo: mcu/CH32V203/config.c   (pairs with mcu/CH32V203/config.h)
//
// The portable ESCape32 code (main.c, io.c, telem.c, prog.c, util.c) calls the
// functions below.  Their NAMES must stay the same; only the insides are chip
// specific:
//     init()       clocks, pins, timers, ADC, DMA, interrupts (called once)
//     starttick()  start the 16 kHz system tick           (NEW, see notes)
//     hsictl()     trim the internal oscillator
//     compctl()    point the BEMF comparator at a motor phase
//     io_serial()  throttle pin -> serial protocol         (only with IO_PA2)
//     io_analog()  throttle pin -> analog                  (stub on this board)
//     adctrig()    start one ADC run
//   + the interrupt vectors (RISC-V needs explicit wrappers, see bottom).
//
// HARDWARE PLAN (all taken from config.h):
//   TIM1  PA8/9/10 high-side, PB13/14/15 low-side (default mapping, no remap)
//   TIM2  IFTIM: PA0/PA1 capture the comparator outputs (partial remap 2),
//         CH3 (internal only) is the commutation delay timer, TRGO -> TIM1 COM
//   TIM3  IOTIM: throttle on PB4 (partial remap), DMA1 ch6
//   USART1 telemetry on PB6 (remap), DMA1 ch4 TX / ch5 RX
//   USART2 optional serial throttle on PA2, DMA1 ch7 TX
//   ADC1  voltage on IN5 (PA5) + internal temperature + internal Vref, DMA1 ch1
//   OPA1/OPA2 used as open-loop comparators for BEMF
// =============================================================================

#include "ch32v20x.h"
#include "ch32v20x_adc.h" // Get_CalibrationValue()
#include "ch32v20x_opa.h" // OPA_Init(), OPA_Cmd()
#include "common.h"

// -----------------------------------------------------------------------------
// Interrupt handler plumbing
// -----------------------------------------------------------------------------
// RISC-V handlers need the "interrupt" attribute so GCC saves registers and
// returns with mret.  WCH's "fast" variant relies on the hardware register
// stacking (HPE) that the SDK startup file switches on (csrw 0x804).
// With mainline GCC build with -DIRQ='__attribute__((interrupt))'  *and* remove
// the 0x804 write from the startup file (they must match!).
#ifndef IRQ
#define IRQ __attribute__((interrupt("WCH-Interrupt-fast")))
#endif
// ISR(name) declares + defines a vector.  (The prototype keeps
// -Wmissing-prototypes quiet.)  The names must equal the ones in
// startup_ch32v20x_D6.S, otherwise the weak default handler (endless loop) runs.
#define ISR(name)        \
	IRQ void name(void); \
	IRQ void name(void)


// Interrupt priorities, smaller = more urgent.  Commutation must never wait.
#define PRIO_COMM 0x00 // TIM1 COM, TIM2 capture
#define PRIO_IO 0x40   // throttle capture / DSHOT DMA
#define PRIO_SLOW 0x80 // ADC, telemetry, tick, software interrupt

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------
#define ADC_CH_TEMP 16	// internal temperature sensor
#define ADC_CH_VREF 17	// internal reference (~1.2 V), used to measure VDD
#define VREFINT_MV 1200 // nominal Vref in mV

// Internal temperature sensor: ONE factory point at 0x1FFFF720 (same address and
// formula as WCH's TempSensor_Volt_To_Temper() in ch32v20x_adc.c):
//   low 16 bits = sensor mV at the reference temperature, high 16 bits = that temperature (C)
// slope ~4.3 mV/C.  ESCape32 wants QUARTER degrees -> "* 4".
#define TS_CAL (*(volatile uint32_t *)0x1FFFF720)
#define TEMP_FUNC(mv) ((((int)(TS_CAL >> 16) & 0xffff) - (((mv) - (int)(TS_CAL & 0xffff)) * 10 + 21) / 43) * 4)

// GPIO 4-bit pin codes (CNF[1:0] MODE[1:0]) for CFGLR/CFGHR
#define PIN_ANALOG 0x0	 // analog input
#define PIN_FLOAT_IN 0x4 // digital input, floating
#define PIN_PULL_IN 0x8	 // digital input with pull (ODR bit selects up/down)
#define PIN_AF_PP 0xb	 // peripheral output, push-pull, 50 MHz
#define PIN_AF_OD 0xf	 // peripheral output, open-drain, 50 MHz

// AFIO->PCFR1 remap fields (same layout as STM32F1)
#define PCFR1_USART1_REMAP (1u << 2) // 1 = PB6/PB7
#define PCFR1_TIM2_MASK (3u << 8)
#define PCFR1_TIM2_PARTIAL2 (2u << 8) // 10b: CH1 PA0, CH2 PA1, CH3 PB10, CH4 PB11
#define PCFR1_TIM3_MASK (3u << 10)
#define PCFR1_TIM3_PARTIAL (2u << 10) // 10b: CH1 PB4, CH2 PB5, CH3 PB0, CH4 PB1

// DMA1->INTFCR: 4 bits per channel (GIF, TCIF, HTIF, TEIF); TCIF of channel n (1..7)
#define DMA_TCIF(n) (2u << (((n) - 1) * 4))

static uint8_t len, ain; // ADC conversions per run; 1 if analog throttle is in the list
static uint16_t buf[6];	 // DMA target: up to 3 sensors + analog + temp + vref
static int adcofs;		 // ADC offset from the hardware calibration

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static void pin(GPIO_TypeDef *g, int n, uint32_t mode)
{
	volatile uint32_t *r = n < 8 ? &g->CFGLR : &g->CFGHR;
	int s = (n & 7) * 4;
	*r = (*r & ~(0xfu << s)) | (mode << s);
}

static void pullup(GPIO_TypeDef *g, int n)
{
	pin(g, n, PIN_PULL_IN);
	g->BSHR = 1u << n; // ODR=1 on an input-with-pull pin selects the pull-UP
}

// Put an ADC channel's pin into analog mode (reset default is "floating input")
static void adcpin(int ch)
{
	if (ch < 8)
		pin(GPIOA, ch, PIN_ANALOG); // IN0..7  = PA0..7
	else if (ch < 10)
		pin(GPIOB, ch - 8, PIN_ANALOG); // IN8,9   = PB0,1
	else if (ch < 16)
		pin(GPIOC, ch - 10, PIN_ANALOG); // IN10..15 = PC0..5
}

// Conversion slot idx (0..15) of the regular sequence := channel ch.
// Slots 0-5 RSQR3, 6-11 RSQR2, 12-15 RSQR1, 5 bits each.
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

// Raw ADC code -> offset-corrected code (same rule as WCH's Get_ConversionVal)
static int adccorr(int raw)
{
	if (raw <= 0)
		return 0;
	if (raw >= 4095)
		return 4095;
	return clamp(raw + adcofs, 0, 4095);
}

// -----------------------------------------------------------------------------
// BEMF comparator map
// -----------------------------------------------------------------------------
// The CH32V203 has no real comparator.  We use each op-amp open loop (output
// saturates high/low) and the board wires its OUT pin to a TIM2 capture pin:
//     OPA1 -> PB1 --(PCB trace)--> PA0 = TIM2_CH1
//     OPA2 -> PA4 --(PCB trace)--> PA1 = TIM2_CH2
// An op-amp has no polarity bit and no timer link, so:
//   * "invert polarity" (x & 4 in compctl) is done by choosing the CAPTURE EDGE
//     (CCxP in TIM2->CCER),
//   * the phase is chosen by the op-amp input selectors PSEL / NSEL.
// ESCape32 phase codes 1,2,3 -> COMP_IN1..3 (from COMP_MAP) -> board input 1..3
// -> one row of cmpmap[].  >>> THIS TABLE MUST MATCH YOUR SCHEMATIC <<<
typedef struct
{
	uint8_t opa;  // OPA1 / OPA2
	uint8_t psel; // positive input select (CHP0 / CHP1)
	uint8_t nsel; // negative input select (CHN0 / CHN1)
	uint8_t ch;	  // TIM2 capture channel fed by this op-amp (1 or 2)
} Cmp;

static const Cmp cmpmap[3] = {
	{OPA1, CHP1, CHN0, 1}, // board input 1 = phase A: PB0 (OPA1_CH1P) vs virtual neutral PB11 (OPA1_CH0N)
	{OPA2, CHP1, CHN0, 2}, // board input 2 = phase B: PA7 (OPA2_CH1P) vs virtual neutral PB10 (OPA2_CH0N)
	{OPA1, CHP0, CHN1, 1}, // board input 3 = phase C: PA6 (OPA1_CH1N) vs ??? on CHP0  -- UNRESOLVED, see notes
};

// 0 = op-amp output is HIGH when phase > neutral (phase on P, neutral on N).
// Set -DCMP_OUT_INV if a row of cmpmap has the phase on the N side.
#ifndef CMP_OUT_INV
#define CMP_OUT_INV 0
#endif

// -----------------------------------------------------------------------------
// init()
// -----------------------------------------------------------------------------
// SystemInit() has already run (system_ch32v20x.c, SYSCLK_FREQ_96MHz_HSI):
// HCLK = PCLK2 = 96 MHz, PCLK1 = 48 MHz.  APB1 timers get 2 x PCLK1 = 96 MHz
// = CLK in config.h.  Verify once with a scope (toggle a pin from a timer).
void init(void)
{
	// ---- 1. Clocks ----------------------------------------------------------
	RCC->CFGR0 = (RCC->CFGR0 & ~RCC_ADCPRE) | RCC_ADCPRE_DIV8; // ADC clock 96/8 = 12 MHz (limit ~14)
	RCC->AHBPCENR |= RCC_DMA1EN;
	RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPAEN | RCC_IOPBEN | RCC_IOPCEN | RCC_ADC1EN | RCC_TIM1EN | RCC_USART1EN;
	RCC->APB1PCENR |= RCC_TIM2EN | RCC_TIM3EN | RCC_APB1Periph_TIM4 | RCC_WWDGEN; // WWDG = ESCape32's "reset now" trick

	// ---- 2. Pin remaps (AFIO clock is on now) --------------------------------
	AFIO->PCFR1 = (AFIO->PCFR1 & ~(PCFR1_TIM2_MASK | PCFR1_TIM3_MASK)) | PCFR1_USART1_REMAP | PCFR1_TIM2_PARTIAL2 | PCFR1_TIM3_PARTIAL;
	// TIM1 stays on the default mapping: CH1/2/3 = PA8/9/10, CH1N/2N/3N = PB13/14/15

	// ---- 3. GPIO -----------------------------------------------------------------
	// Motor bridge.  Drive the latch LOW first, then hand the pin to TIM1, so the
	// gates never see a glitch.  (Gate-driver inputs should still have pull-downs.)
	GPIOA->BCR = (1u << 8) | (1u << 9) | (1u << 10);
	GPIOB->BCR = (1u << 13) | (1u << 14) | (1u << 15);
	pin(GPIOA, 8, PIN_AF_PP);  // TIM1_CH1
	pin(GPIOA, 9, PIN_AF_PP);  // TIM1_CH2
	pin(GPIOA, 10, PIN_AF_PP); // TIM1_CH3
	pin(GPIOB, 13, PIN_AF_PP); // TIM1_CH1N
	pin(GPIOB, 14, PIN_AF_PP); // TIM1_CH2N
	pin(GPIOB, 15, PIN_AF_PP); // TIM1_CH3N

	// Op-amp pins: analog mode (inputs and outputs)
	pin(GPIOB, 0, PIN_ANALOG);	// OPA1_CH1P  phase A
	pin(GPIOA, 7, PIN_ANALOG);	// OPA2_CH1P  phase B
	pin(GPIOA, 6, PIN_ANALOG);	// OPA1_CH1N  phase C
	pin(GPIOB, 11, PIN_ANALOG); // OPA1_CH0N  neutral
	pin(GPIOB, 10, PIN_ANALOG); // OPA2_CH0N  neutral (also TIM2_CH3 remap target: never enable CC3E!)
	pin(GPIOB, 1, PIN_ANALOG);	// OPA1_OUT1
	pin(GPIOA, 4, PIN_ANALOG);	// OPA2_OUT1
	// Capture inputs, wired on the PCB to the two op-amp outputs
	pin(GPIOA, 0, PIN_FLOAT_IN); // TIM2_CH1
	pin(GPIOA, 1, PIN_FLOAT_IN); // TIM2_CH2

	// Throttle: TIM3_CH1 on PB4, idles high
	pullup(GPIOB, 4);

	// Telemetry: USART1_TX on PB6.  telem.c runs it half-duplex (HDSEL) on one wire, so
	// the pin must be OPEN-DRAIN (needs a pull-up on the line).  PB7 is not used.
	pin(GPIOB, 6, PIN_AF_OD);

	// ---- 4. IFTIM = TIM2 and its link to TIM1 ---------------------------------------
	// Chain: BEMF edge -> TIM2 CH1/CH2 capture -> (ESCape32 loads CH3CVR = delay) ->
	//        OC3REF rising at the match -> TRGO -> TIM1 TRGI -> COM event -> new commutation.
	TIM1->SMCFGR = TIM_TS_0;										  // TIM1 trigger input = ITR1 (= TIM2 TRGO)  VERIFY in the RM interconnect table
	TIM2->CTLR2 = TIM_MMS_2 | TIM_MMS_1;							  // TIM2 master: TRGO = OC3REF
	TIM2->CHCTLR1 = TIM_CC1S_0 | TIM_CC2S_0;						  // CH1, CH2 = capture (filters are set later via IFTIM_ICM1/2/3)
	TIM2->CHCTLR2 = TIM_OC3PE | TIM_OC3M_2 | TIM_OC3M_1 | TIM_OC3M_0; // CH3 = PWM mode 2 + preload (IFTIM_OCR)
	TIM2->CCER = 0;													  // captures are armed by compctl()

	// ---- 5. ADC1 ----------------------------------------------------------------------
	ADC1->CTLR1 = ADC_SCAN; // one start converts the whole list
	ADC1->CTLR2 = ADC_ADON | ADC_TSVREFE;
	for (volatile int i = 0; i < 2000; i++)
		;																	   // power-up / sensor settling
	adcofs = Get_CalibrationValue(ADC1);									   // WCH routine: runs the HW calibration, returns the offset
	ADC1->CTLR2 = ADC_ADON | ADC_TSVREFE | ADC_EXTSEL | ADC_EXTTRIG | ADC_DMA; // software start, results to DMA

	// Sample time per channel (3 bits): 0=1.5 1=7.5 2=13.5 3=28.5 4=41.5 5=55.5 6=71.5 7=239.5 cycles.
	// External pins: 71.5 (~7 us).  Temp sensor / Vref (ch16, ch17): 239.5 (~21 us) because they
	// are high impedance sources and need >= ~17 us.
	{
		uint32_t s2 = 0, s1 = 0;
		for (int c = 0; c < 10; c++)
			s2 |= 6u << (c * 3); // ch0..9   -> SAMPTR2
		for (int c = 10; c < 16; c++)
			s1 |= 6u << ((c - 10) * 3); // ch10..15 -> SAMPTR1
		s1 |= 7u << ((ADC_CH_TEMP - 10) * 3) | 7u << ((ADC_CH_VREF - 10) * 3);
		ADC1->SAMPTR2 = s2;
		ADC1->SAMPTR1 = s1;
	}

	// Conversion list: [sensors in config.h order] [analog throttle?] [temperature] [vref]
	len = 0;
	for (int i = 0; i < SENS_CNT; i++)
	{
		int ch = (SENS_CHAN >> (5 * i)) & 0x1f;
		adcpin(ch);
		sq(len++, ch);
	}
#ifdef ANALOG_CHAN
	if (IO_ANALOG)
	{
		adcpin(ANALOG_CHAN);
		sq(len++, ANALOG_CHAN);
		ain = 1;
	}
#endif
	sq(len++, ADC_CH_TEMP);
	sq(len++, ADC_CH_VREF);
	ADC1->RSQR1 = (ADC1->RSQR1 & ~ADC_L) | ((uint32_t)(len - 1) << 20);

	// DMA1 channel 1 is hard-wired to ADC1 (no DMAMUX on this chip)
	DMA1_Channel1->PADDR = (uint32_t)&ADC1->RDATAR;
	DMA1_Channel1->MADDR = (uint32_t)buf;

	// ---- 6. Interrupts (only vectors we really define below!) -------------------------
	NVIC_SetPriority(TIM1_TRG_COM_IRQn, PRIO_COMM);
	NVIC_SetPriority(TIM2_IRQn, PRIO_COMM);
	NVIC_SetPriority(TIM3_IRQn, PRIO_IO);
	NVIC_SetPriority(DMA1_Channel6_IRQn, PRIO_IO);
	NVIC_SetPriority(USART1_IRQn, PRIO_SLOW);
	NVIC_SetPriority(DMA1_Channel4_IRQn, PRIO_SLOW);
	NVIC_SetPriority(DMA1_Channel1_IRQn, PRIO_SLOW);
	NVIC_EnableIRQ(TIM1_TRG_COM_IRQn);	// tim1_com_isr
	NVIC_EnableIRQ(TIM2_IRQn);			// iftim_isr
	NVIC_EnableIRQ(TIM3_IRQn);			// tim3_io_isr (+ hall)
	NVIC_EnableIRQ(DMA1_Channel6_IRQn); // iotim_dma_isr
	NVIC_EnableIRQ(USART1_IRQn);		// usart1_isr
	NVIC_EnableIRQ(DMA1_Channel4_IRQn); // usart1_tx_dma_isr
	NVIC_EnableIRQ(DMA1_Channel1_IRQn); // ADC results
}

// -----------------------------------------------------------------------------
// starttick(): 16 kHz SysTick + software interrupt.  Call it from main() in place of the
// ARM-only "STK_RVR / STK_CVR / STK_CSR" lines and nvic_set_priority(NVIC_PENDSV_IRQ,...).
// -----------------------------------------------------------------------------
void starttick(void)
{
	NVIC_SetPriority(SysTicK_IRQn, PRIO_SLOW);		   // WCH spells it SysTicK_IRQn  VERIFY in ch32v20x.h
	NVIC_SetPriority(Software_IRQn, PRIO_SLOW + 0x40); // "PendSV" replacement, lower than the tick
	NVIC_EnableIRQ(SysTicK_IRQn);
	NVIC_EnableIRQ(Software_IRQn);
	SysTick->CTLR = 0;
	SysTick->SR = 0;
	SysTick->CNT = 0;
	SysTick->CMP = CLK_KHZ / 16 - 1; // 16 kHz
	SysTick->CTLR = 0xf;			 // STE | STIE | STCLK (HCLK) | STRE (auto reload)
}

// -----------------------------------------------------------------------------
// hsictl(): nudge the HSI trim by x steps.  CH32: 5-bit HSITRIM in RCC->CTLR[7:3].
// -----------------------------------------------------------------------------
void hsictl(int x)
{
	uint32_t cr = RCC->CTLR;
	int tv = (cr & RCC_HSITRIM) >> 3;
	RCC->CTLR = (cr & ~RCC_HSITRIM) | (uint32_t)clamp(tv + x, 0, 0x1f) << 3;
}

// -----------------------------------------------------------------------------
// compctl(x): x & 3 = phase code (0 = none), x & 4 = rising BEMF (see main.c table).
// Called from the COM interrupt right after each commutation.
// -----------------------------------------------------------------------------
void compctl(int x)
{
	int ph = x & 3;
	TIM2->CCER &= ~(TIM_CC1E | TIM_CC2E); // no captures while the op-amp input switches
	if (!ph)
	{
		OPA_Cmd(OPA1, DISABLE);
		OPA_Cmd(OPA2, DISABLE);
		return;
	}
	const Cmp *c = &cmpmap[(ph == 1 ? COMP_IN1 : ph == 2 ? COMP_IN2
														 : COMP_IN3) -
						   1];
	OPA_InitTypeDef o;
	o.OPA_NUM = c->opa;
	o.Mode = OUT_IO_OUT1; // both op-amps drive their OUT1 pin (PB1 / PA4)
	o.PSEL = c->psel;
	o.NSEL = c->nsel;
	OPA_Init(&o);			 // note: leaves the op-amp disabled ...
	OPA_Cmd(c->opa, ENABLE); // ... so enable it again
	int rising = (x & 4) != 0;
	if (CMP_OUT_INV)
		rising = !rising;
	// op-amp out rises when BEMF rises -> capture the matching edge (CCxP = 1 means falling edge)
	if (c->ch == 1)
		TIM2->CCER = TIM_CC1E | (rising ? 0 : TIM_CC1P);
	else
		TIM2->CCER = TIM_CC2E | (rising ? 0 : TIM_CC2P);
}

// -----------------------------------------------------------------------------
// Throttle pin -> serial / analog
// -----------------------------------------------------------------------------
#ifdef IO_PA2
// Throttle wire becomes a serial link on USART2 (PA2).  NOTE: the throttle capture pin is PB4
// but USART2_TX is PA2 -> the board must connect both pins to the same signal pad.
void io_serial(void)
{
#ifdef HALL_MAP
	TIM3->DMAINTENR &= ~(TIM_CC1DE | TIM_CC1IE); // hall still needs TIM3
	TIM3->CCER &= ~TIM_CC1E;
#else
	TIM3->DMAINTENR = 0; // stop ALL TIM3 interrupts (incl. the ~66 ms timeout)
	TIM3->CTLR1 &= ~TIM_CEN;
#endif
	DMA1_Channel6->CFGR = 0; // free DMA1 ch6: now it can serve USART2_RX (config.h: #define USART2_RX_DMA 6)
	NVIC_DisableIRQ(DMA1_Channel6_IRQn);
	NVIC_ClearPendingIRQ(DMA1_Channel6_IRQn);
	NVIC_ClearPendingIRQ(TIM3_IRQn);
	RCC->APB1PCENR |= RCC_USART2EN;
	pin(GPIOA, 2, PIN_AF_OD); // half-duplex single wire -> open-drain
	NVIC_SetPriority(USART2_IRQn, PRIO_IO);
	NVIC_EnableIRQ(USART2_IRQn);
}
#endif

// This board's throttle pin (PB4) has no ADC channel, so IO_ANALOG is 0 and this is never
// called at run time.  It only has to exist because io.c references it.
void io_analog(void)
{
}
void iopin(int out)
{
	if (out)
		pin(GPIOB, 4, PIN_AF_OD); // timer drives the wire, open-drain; the FC/adapter pull-up makes "high"
	else
		pullup(GPIOB, 4); // listen, with pull-up
}
// -----------------------------------------------------------------------------
// ADC
// -----------------------------------------------------------------------------
void adctrig(void)
{
	if (DMA1_Channel1->CFGR & DMA_CFGR1_EN) // previous run not finished
		return;
	DMA1_Channel1->CNTR = len;
	DMA1_Channel1->CFGR = DMA_CFGR1_EN | DMA_CFGR1_TCIE | DMA_CFGR1_MINC | DMA_CFGR1_PSIZE_0 | DMA_CFGR1_MSIZE_0; // periph->mem, 16 bit
	ADC1->CTLR2 |= ADC_SWSTART;
}

// -----------------------------------------------------------------------------
// Interrupt vectors
// -----------------------------------------------------------------------------
ISR(DMA1_Channel1_IRQHandler)
{ // ADC list finished and copied into buf[]
	DMA1->INTFCR = DMA_TCIF(1);
	DMA1_Channel1->CFGR = 0;
	int i = 0, u = 0, v = 0, c = 0, a = 0;
	int raw[6];
	for (int k = 0; k < len; k++)
		raw[k] = adccorr(buf[k]);
	int vref = raw[len - 1];
	if (!vref)
		return;
	int r = VREFINT_MV * 4096 / vref; // VDD in mV
#define MV(x) ((x) * r >> 12)
#if SENS_CNT >= 2
	c = MV(raw[i++]);
#endif
#if SENS_CNT >= 1
	v = MV(raw[i++]);
#endif
#if SENS_CNT >= 3
	u = MV(raw[i++]);
#endif
	if (ain)
		a = MV(raw[i++]);
	adcdata(TEMP_FUNC(MV(raw[i])), u, v, c, a);
#undef MV
}

ISR(TIM3_IRQHandler)
{ // throttle capture (+ hall sensors if present): one vector, two users
	tim3_io_isr();
#ifdef HALL_MAP
	tim3_hall_isr();
#endif
}

ISR(TIM2_IRQHandler) { iftim_isr(); }			   // BEMF zero-cross
ISR(TIM1_TRG_COM_IRQHandler) { tim1_com_isr(); }   // commutation
ISR(DMA1_Channel6_IRQHandler) { iotim_dma_isr(); } // DSHOT
ISR(USART1_IRQHandler) { usart1_isr(); }		   // telemetry
ISR(DMA1_Channel4_IRQHandler) { usart1_tx_dma_isr(); }
#ifdef IO_PA2
ISR(USART2_IRQHandler) { usart2_isr(); }
#endif

ISR(SysTick_Handler)
{
	SysTick->SR = 0; // clear the compare flag
	sys_tick_handler();
}

ISR(SW_Handler) { pend_sv_handler(); } // software interrupt = ARM PendSV replacement

ISR(HardFault_Handler) { hard_fault_handler(); }