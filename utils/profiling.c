
#include <stdint.h>
typedef struct {
    uint32_t    r0, r1, r2, r3, r12, lr, pc, xpsr;
} ExceptionRegisters_t;

#define PROF_ERR	32
#define PROF_MASK (~(PROF_ERR - 1))
#define PROF_CNT	20

uint32_t s_ignrList[] = {
	0,
	// todo: add pc address ranges that you do not care, such as idle function.
};

typedef struct {
	uint32_t baseAddr;	// (aligned) base address range of PC sample
	uint32_t hitCnt;    // how many hits (note, a decay mecahnism automatically drop hitCnt)
	uint32_t hitRatio;	// 10-bit resolution hit ratio, i.e., estimation of CPU usage
	uint32_t rsvd;
} ProfUnit_t;

typedef struct {
	uint8_t decayNdx;
	uint32_t profCnt;
	ProfUnit_t items[PROF_CNT];
}Prof_t;
Prof_t s_prof;

void _ProfOnHit(ProfUnit_t *pItem, uint32_t pc) 
{
	pItem->baseAddr = pc & PROF_MASK;
	s_prof.profCnt+= 0x02;
	pItem->hitCnt += 0x02;
	pItem->hitRatio = (uint32_t)(((uint64_t)(pItem->hitCnt) << 10) / s_prof.profCnt);
	// sort items descending
	ProfUnit_t tmpItem;	
	for (;pItem != s_prof.items && pItem[0].hitCnt > pItem[-1].hitCnt; pItem--) {
		tmpItem = pItem[0]; pItem[0] = pItem[-1] ; pItem[-1] = tmpItem;
	}
}

void Profiling(uint32_t pc) 
{
	uint32_t i;
	ProfUnit_t *pItem = &s_prof.items[0];
	for (i=0; i<ARRAY_SIZE(s_ignrList); i++) {
		if (pc - s_ignrList[i] < PROF_ERR)
			return;
	}
	if (s_prof.items[s_prof.decayNdx].hitCnt != 0) {
		s_prof.items[s_prof.decayNdx].hitCnt--;
		s_prof.profCnt--;
	}
	if (++s_prof.decayNdx == PROF_CNT)
		s_prof.decayNdx = 0;
	uint32_t freeNdx = PROF_CNT;
	for (i=0, pItem = s_prof.items; i<PROF_CNT; i++, pItem++) {
		if (pItem->baseAddr == (pc & PROF_MASK)) {
			_ProfOnHit(pItem, pc);
			break;
		} else if (freeNdx == PROF_CNT && pItem->hitCnt == 0){
			freeNdx = i;
		}
	}
	if (i == PROF_CNT && freeNdx < PROF_CNT) {
		// does not find, allocate for new
		_ProfOnHit(s_prof.items + freeNdx, pc);
	}
}

void SysTick_C_Handler(ExceptionRegisters_t *regs) {
	Profiling(regs->pc); 
	// todo: other systick handler code
}

#ifdef __CC_ARM
__asm void SysTick_Handler(void) {
	IMPORT	SysTick_C_Handler
	PRESERVE8
	tst lr, #4 
    ite eq                 // Tell the assembler that the nest 2 instructions are if-then-else	
	mrseq r0, msp		   // Make R0 point to main stack pointer
	mrsne r0, psp		   // Make R0 point to process stack pointer
	push   {lr}
	bl SysTick_C_Handler  // Off to C land
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
