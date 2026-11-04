/* Minimal Cortex-M33 start-up for the QEMU bench: FPU on, .bss cleared (QEMU loads .data). */
#include <stdint.h>
extern uint32_t __bss_start__, __bss_end__, __stack_top;
int main(void);
void _exit(int);
void reset_handler(void)
{
	*(volatile uint32_t*)0xE000ED88 |= 0xFu << 20; /* CPACR: CP10, CP11 full access */
	__asm volatile("dsb\n isb");
	for (uint32_t* p = &__bss_start__; p < &__bss_end__; p++) *p = 0;
	_exit(main());
}
static void fault(void) { for (;;) {} }
__attribute__((section(".vectors"), used)) static const void* const vectors[16] = {
	&__stack_top, (void*)reset_handler, (void*)fault, (void*)fault, (void*)fault, (void*)fault, (void*)fault,
};
