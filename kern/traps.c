#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <trap.h>

extern void handle_int(void);
extern void handle_tlb(void);
extern void handle_sys(void);
extern void handle_mod(void);
extern void handle_reserved(void);
extern void handle_adel(void);
extern void handle_ades(void);

void (*exception_handlers[32])(void) = {
    [0 ... 31] = handle_reserved,
    [0] = handle_int,
    [2 ... 3] = handle_tlb,
	[4] = handle_adel,
	[5] = handle_ades,
#if !defined(LAB) || LAB >= 4
    [1] = handle_mod,
    [8] = handle_sys,
#endif
};

/* Overview:
 *   The fallback handler when an unknown exception code is encountered.
 *   'genex.S' wraps this function in 'handle_reserved'.
 */
void do_reserved(struct Trapframe *tf) {
	print_tf(tf);
	panic("Unknown ExcCode %2d", (tf->cp0_cause >> 2) & 0x1f);
}

void do_adel(struct Trapframe *tf){
	// load
	u_int va = tf->cp0_epc;
	struct Page *p = page_lookup(curenv->env_pgdir, va, NULL);
	u_int kva = page2kva(p) | (va & 0xfff);
	u_int inst = *((u_int*)kva);

	u_int imm;
	imm = inst & 0xffff;
//	base = (inst >> 21) & ((1 << 5) - 1);
	u_int badaddr = tf->cp0_badvaddr;
	u_int bios = badaddr & 0x3;
	imm -= bios;
	inst = (inst & ~(0xffff)) | (imm & 0xffff);
	printk("AdEL handled, new imm is : %04x\n", imm & 0xffff);
	*((u_int*)kva) = inst;
}

void do_ades(struct Trapframe *tf){

	u_int va = tf->cp0_epc;
	struct Page *p = page_lookup(curenv->env_pgdir, va, NULL);
	u_long kva = page2kva(p) | (va & 0xfff);
	u_int inst = *((u_int*)kva);

	u_int imm;
	imm = inst & 0xffff;
//	base = (inst >> 21) & ((1 << 5) - 1);
	u_int badaddr = tf->cp0_badvaddr;
	u_int bios = badaddr & 0x3;
	imm -= bios;
	inst = (inst & ~(0xffff)) | (imm & 0xffff);
	printk("AdES handled, new imm is : %04x\n", imm & 0xffff);
	*((u_int*)kva) = inst;
}
