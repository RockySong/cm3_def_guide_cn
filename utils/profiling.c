#include <stdint.h>

typedef struct {
    uint32_t    r0, r1, r2, r3, r12, lr, pc, xpsr;
} ExceptionRegisters_t;

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))
#define PROF_CNT		15
#define PROF_ERR		64
#define PROF_MASK 		(~(PROF_ERR - 1))
#define PROF_HITCNT_INC	10
// whether hitCnt should decay, faster decay makes most time consuming functions
// seems to have even more hit count
#define PROF_DECAY		
extern void ELCDIF_GetInterruptStatus(void);
extern void PXP_GetStatusFlags(void);
uint32_t s_ignrList[] = {
	0,
	// todo: add pc address ranges that you do not care, such as idle function.
};

typedef struct {
	uint32_t baseAddr;	// (aligned) base address range of PC sample
	uint32_t hitCnt;    // hit count (a decay mecahnism automatically drops it)
	uint32_t hitRatio;	// 10-bit resolution hit ratio, 
	uint32_t rsvd;		// reserved for better view in memory window
} ProfUnit_t;

typedef struct {
	uint8_t decayNdx;  // which item to decay its hitCnt 
	uint32_t profCnt;  // totoal hit count of profiling
	ProfUnit_t items[PROF_CNT];
}Prof_t;
Prof_t s_prof;

void _ProfOnHit(ProfUnit_t *pItem, uint32_t pc) 
{
	pItem->baseAddr = pc & PROF_MASK;
	s_prof.profCnt+= PROF_HITCNT_INC;
	pItem->hitCnt += PROF_HITCNT_INC;
	pItem->hitRatio = 
		(uint32_t)(((uint64_t)(pItem->hitCnt) << 10) / s_prof.profCnt);
	// sort items descending
	ProfUnit_t tmpItem;	
	for (;pItem != s_prof.items && pItem[0].hitCnt > pItem[-1].hitCnt; pItem--) 
	{
		tmpItem = pItem[0]; pItem[0] = pItem[-1] ; pItem[-1] = tmpItem;
	}
}
void ProfUpdateRate(float res)
{
  uint32_t i; 
  float profCnt = s_prof.profCnt;
  ProfUnit_t *pItem = s_prof.items;
  for (i=0; i<PROF_CNT; i++, pItem++) {
	pItem->hitRatio = 
	  (uint32_t)( (float)(pItem->hitCnt) * res / profCnt);
  }
}
void ProfReset(void)
{
  memset(&s_prof, 0, sizeof(s_prof));
}
void Profiling(uint32_t pc) 
{
	uint32_t i;
	ProfUnit_t *pItem = &s_prof.items[0];
	// filter ignore list
	for (i=0; i<ARRAY_SIZE(s_ignrList); i++) {
		if (pc - s_ignrList[i] < PROF_ERR)
			return;
	}
#ifdef PROF_DECAY
	// apply decaying, we do not decay to zero, this means PC samples do not
	// get removed automatically, only can be pushed by later new more frequent
	// hit PC samples 
	if (s_prof.items[s_prof.decayNdx].hitCnt > 1) {
		s_prof.items[s_prof.decayNdx].hitCnt--;
		s_prof.profCnt--;
	}
	if (++s_prof.decayNdx == PROF_CNT)
		s_prof.decayNdx = 0;
#endif	
	uint32_t freeNdx = PROF_CNT;
	// >>> traverse to search existing same PC sample
	for (i=0, pItem = s_prof.items; i<PROF_CNT; i++, pItem++) {
		if (pItem->baseAddr == (pc & PROF_MASK)) {
			_ProfOnHit(pItem, pc);
			break;
		} else if (freeNdx == PROF_CNT && pItem->hitCnt == 0){
			// record empty item, in case need to use for new PC sample
			freeNdx = i;
		}
	}
	if (i == PROF_CNT && freeNdx < PROF_CNT) {
		// does not find, allocate for new
		_ProfOnHit(s_prof.items + freeNdx, pc);
	} else {
	  // replace the last one. We must give new samples chance to compete to
	  // get into the list.
	  freeNdx = PROF_CNT - 1;
	  s_prof.profCnt -= s_prof.items[freeNdx].hitCnt;
	  s_prof.items[freeNdx].hitCnt = 0;
	  _ProfOnHit(s_prof.items + freeNdx, pc);
	}
}

volatile uint32_t g_tick;
static uint32_t s_prescale;
#define STICK_PRESCALE	1
void SysTick_C_Handler(ExceptionRegisters_t *regs) {
    
	Profiling(regs->pc); 
	if (--s_prescale != 0)
	  return;
	s_prescale = STICK_PRESCALE;
	g_tick++;
	// todo: Further tick handling code
}

void SysTickInit(void)
{
	NVIC_SetPriority(SysTick_IRQn, 0);
    SysTick_Config(CLOCK_GetFreq(kCLOCK_CoreSysClk) / 1000U / STICK_PRESCALE);
	s_prescale = STICK_PRESCALE; 
}

#ifdef __CC_ARM
__asm void SysTick_Handler(void) {
	IMPORT	SysTick_C_Handler
	PRESERVE8
	tst lr, #4 
    ite eq 
	mrseq r0, msp
	mrsne r0, psp
	push   {lr}
	bl SysTick_C_Handler
	pop    {lr}
	bx	   lr
}
#else
__attribute__((naked))
void SysTick_Handler(void) {
	__asm volatile (
		" tst lr, #4	\n" 		// Test Bit 3 to see which stack pointer we should use.
		" ite eq		\n" 		// Tell the assembler that the nest 2 instructions are if-then-else
		" mrseq r0, msp \n" 		// Make R0 point to main stack pointer
		" mrsne r0, psp \n" 		// Make R0 point to process stack pointer
		" push	{r4-r11, lr} \n"
		" mov	r1, sp	\n"
		" bl SysTick_C_Handler \n" // Off to C land
		" pop  {r4-r11, lr}  \n"
		" bx   lr  \n"
	);
}
#endif
