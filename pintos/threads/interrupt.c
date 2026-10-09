#include "threads/interrupt.h"
#include <debug.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "threads/flags.h"
#include "threads/intr-stubs.h"
#include "threads/io.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/gdt.h"
#endif

/* x86_64 인터럽트의 개수. */
#define INTR_CNT 256

/* FUNCTION을 호출하는 gate를 생성함.

이 gate는 descriptor privilege level DPL을 가지며, 이는
프로세서가 DPL 또는 그보다 번호가 작은 ring에 있을 때
의도적으로 호출할 수 있다는 뜻임.  실제로 DPL==3이면
user mode에서 gate를 호출할 수 있고, DPL==0이면 그런 호출이
막힘.  user mode에서 발생한 fault와 exception은 DPL==0인
gate도 호출되게 함.

TYPE은 14(interrupt gate) 또는 15(trap gate)여야 함.  차이는
interrupt gate에 진입하면 인터럽트가 비활성화되지만 trap
gate는 그렇지 않다는 것임.  자세한 내용은 [IA32-v3a]
5.12.1.2절 "Flag Usage By Exception- or Interrupt-Handler
Procedure" 참고. */

struct gate {
	unsigned off_15_0 : 16;   // 세그먼트 내 오프셋의 하위 16비트
	unsigned ss : 16;         // 세그먼트 셀렉터
	unsigned ist : 3;        // 인자 개수, interrupt/trap gate는 0
	unsigned rsv1 : 5;        // 예약됨 (0이어야 할 것으로 추정)
	unsigned type : 4;        // 타입(STS_{TG,IG32,TG32})
	unsigned s : 1;           // 0이어야 함 (system)
	unsigned dpl : 2;         // 디스크립터(즉, 새) 특권 레벨
	unsigned p : 1;           // 존재 여부(Present)
	unsigned off_31_16 : 16;  // 세그먼트 내 오프셋의 상위 비트
	uint32_t off_32_63;
	uint32_t rsv2;
};

/* Interrupt Descriptor Table (IDT).  형식은 CPU가 고정해 둠.
   [IA32-v3a]의 5.10절 "Interrupt Descriptor Table (IDT)",
   5.11절 "IDT Descriptors", 5.12.1.2절 "Flag Usage By
   Exception- or Interrupt-Handler Procedure" 참고. */
static struct gate idt[INTR_CNT];

static struct desc_ptr idt_desc = {
	.size = sizeof(idt) - 1,
	.address = (uint64_t) idt
};


#define make_gate(g, function, d, t) \
{ \
	ASSERT ((function) != NULL); \
	ASSERT ((d) >= 0 && (d) <= 3); \
	ASSERT ((t) >= 0 && (t) <= 15); \
	*(g) = (struct gate) { \
		.off_15_0 = (uint64_t) (function) & 0xffff, \
		.ss = SEL_KCSEG, \
		.ist = 0, \
		.rsv1 = 0, \
		.type = (t), \
		.s = 0, \
		.dpl = (d), \
		.p = 1, \
		.off_31_16 = ((uint64_t) (function) >> 16) & 0xffff, \
		.off_32_63 = ((uint64_t) (function) >> 32) & 0xffffffff, \
		.rsv2 = 0, \
	}; \
}

/* 주어진 DPL로 FUNCTION을 호출하는 interrupt gate를 생성함. */
#define make_intr_gate(g, function, dpl) make_gate((g), (function), (dpl), 14)

/* 주어진 DPL로 FUNCTION을 호출하는 trap gate를 생성함. */
#define make_trap_gate(g, function, dpl) make_gate((g), (function), (dpl), 15)



/* 각 인터럽트별 인터럽트 핸들러 함수. */
static intr_handler_func *intr_handlers[INTR_CNT];

/* 디버깅용으로 각 인터럽트에 붙인 이름. */
static const char *intr_names[INTR_CNT];

/* 외부 인터럽트는 타이머처럼 CPU 바깥의 장치가 발생시키는
   인터럽트임.  외부 인터럽트는 인터럽트를 끈 상태로 실행되므로
   중첩되지 않고 pre-empt되지도 않음.  외부 인터럽트의 핸들러는
   sleep할 수 없지만, intr_yield_on_return()을 호출해 인터럽트가
   복귀하기 직전에 새 프로세스를 스케줄하도록 요청할 수는 있음. */
static bool in_external_intr;   /* 외부 인터럽트를 처리 중인가? */
static bool yield_on_return;    /* 인터럽트에서 복귀할 때 yield해야 하는가? */

/* Programmable Interrupt Controller 헬퍼. */
static void pic_init (void);
static void pic_end_of_interrupt (int irq);

/* 인터럽트 핸들러. */
void intr_handler (struct intr_frame *args);

/**
 * @brief 현재 인터럽트 상태를 반환함
 * 
 *
 * @note 아직 뭔지 모르곘음
 */
enum intr_level
intr_get_level (void) {
	uint64_t flags;

	/* flags 레지스터를 프로세서 스택에 push한 뒤, 그 값을 스택에서
	   `flags'로 pop함.  [IA32-v2b] "PUSHF"와 "POP", [IA32-v3a]
	   5.8.1 "Masking Maskable Hardware Interrupts" 참고. */
	asm volatile ("pushfq; popq %0" : "=g" (flags));

	return flags & FLAG_IF ? INTR_ON : INTR_OFF;
}

/* LEVEL에 따라 인터럽트를 활성화하거나 비활성화하고,
   이전 인터럽트 상태를 반환함. */
enum intr_level
intr_set_level (enum intr_level level) {
	return level == INTR_ON ? intr_enable () : intr_disable ();
}

/* 인터럽트를 활성화하고 이전 인터럽트 상태를 반환함. */
enum intr_level
intr_enable (void) {
	enum intr_level old_level = intr_get_level ();
	ASSERT (!intr_context ());

	/* interrupt flag를 설정해 인터럽트를 활성화함.

[IA32-v2b] "STI"와 [IA32-v3a] 5.8.1 "Masking Maskable
Hardware Interrupts" 참고. */
	asm volatile ("sti");

	return old_level;
}


/**
 * @brief 인터럽트를 비활성화하고 이전 인터럽트 상태를 반환함.
 * 
 * @return 이전 인터럽트 상태를 반환함.
 * @note 아직 뭔지 모르곘음
 */
enum intr_level
intr_disable (void) {
	enum intr_level old_level = intr_get_level ();

	/* interrupt flag를 지워 인터럽트를 비활성화함.
	   [IA32-v2b] "CLI"와 [IA32-v3a] 5.8.1 "Masking Maskable
	   Hardware Interrupts" 참고. */
	asm volatile ("cli" : : : "memory");

	return old_level;
}

/* 인터럽트 시스템을 초기화함. */
void
intr_init (void) {
	int i;

	/* 인터럽트 컨트롤러를 초기화함. */
	pic_init ();

	/* IDT를 초기화함. */
	for (i = 0; i < INTR_CNT; i++) {
		make_intr_gate(&idt[i], intr_stubs[i], 0);
		intr_names[i] = "unknown";
	}

#ifdef USERPROG
	/* TSS를 로드함. */
	ltr (SEL_TSS);
#endif

	/* IDT 레지스터를 로드함. */
	lidt(&idt_desc);

	/* intr_names를 초기화함. */
	intr_names[0] = "#DE Divide Error";
	intr_names[1] = "#DB Debug Exception";
	intr_names[2] = "NMI Interrupt";
	intr_names[3] = "#BP Breakpoint Exception";
	intr_names[4] = "#OF Overflow Exception";
	intr_names[5] = "#BR BOUND Range Exceeded Exception";
	intr_names[6] = "#UD Invalid Opcode Exception";
	intr_names[7] = "#NM Device Not Available Exception";
	intr_names[8] = "#DF Double Fault Exception";
	intr_names[9] = "Coprocessor Segment Overrun";
	intr_names[10] = "#TS Invalid TSS Exception";
	intr_names[11] = "#NP Segment Not Present";
	intr_names[12] = "#SS Stack Fault Exception";
	intr_names[13] = "#GP General Protection Exception";
	intr_names[14] = "#PF Page-Fault Exception";
	intr_names[16] = "#MF x87 FPU Floating-Point Error";
	intr_names[17] = "#AC Alignment Check Exception";
	intr_names[18] = "#MC Machine-Check Exception";
	intr_names[19] = "#XF SIMD Floating-Point Exception";
}

/* 인터럽트 VEC_NO가 descriptor privilege level DPL로 HANDLER를
   호출하도록 등록함.  디버깅용으로 인터럽트 이름을 NAME으로
   지정함.  인터럽트 핸들러는 인터럽트 상태가 LEVEL로 설정된
   채 호출됨. */
static void
register_handler (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name) {
	ASSERT (intr_handlers[vec_no] == NULL);
	if (level == INTR_ON) {
		make_trap_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	else {
		make_intr_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	intr_handlers[vec_no] = handler;
	intr_names[vec_no] = name;
}

/* 외부 인터럽트 VEC_NO가 HANDLER를 호출하도록 등록하고,
   디버깅용으로 NAME이라는 이름을 붙임.  핸들러는 인터럽트가
   비활성화된 상태로 실행됨. */
void
intr_register_ext (uint8_t vec_no, intr_handler_func *handler,
		const char *name) {
	ASSERT (vec_no >= 0x20 && vec_no <= 0x2f);
	register_handler (vec_no, 0, INTR_OFF, handler, name);
}

/* 내부 인터럽트 VEC_NO가 HANDLER를 호출하도록 등록하고,
   디버깅용으로 NAME이라는 이름을 붙임.  인터럽트 핸들러는
   인터럽트 상태가 LEVEL인 채로 호출됨.

   핸들러는 descriptor privilege level DPL을 가지며, 이는
   프로세서가 DPL 또는 그보다 번호가 작은 ring에 있을 때
   의도적으로 호출할 수 있다는 뜻임.  실제로 DPL==3이면
   user mode에서 인터럽트를 호출할 수 있고, DPL==0이면 그런
   호출이 막힘.  user mode에서 발생한 fault와 exception은
   DPL==0인 인터럽트도 호출되게 함.  자세한 내용은
   [IA32-v3a] 4.5절 "Privilege Levels"와 4.8.1.1절
   "Accessing Nonconforming Code Segments" 참고. */
void
intr_register_int (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name)
{
	ASSERT (vec_no < 0x20 || vec_no > 0x2f);
	register_handler (vec_no, dpl, level, handler, name);
}

/* 외부 인터럽트를 처리하는 동안에는 true,
   그 외에는 항상 false를 반환함. */
bool
intr_context (void) {
	return in_external_intr;
}

/* 외부 인터럽트를 처리하는 동안, 인터럽트에서 복귀하기 직전에
   새 프로세스로 yield하도록 인터럽트 핸들러에 지시함.
   다른 시점에는 호출하면 안 됨. */
void
intr_yield_on_return (void) {
	ASSERT (intr_context ());
	yield_on_return = true;
}

/* 8259A Programmable Interrupt Controller. */

/* 모든 PC에는 8259A Programmable Interrupt Controller(PIC)
   칩이 두 개 있음.  하나는 포트 0x20과 0x21로 접근하는
   "master"임.  다른 하나는 master의 IRQ 2 라인에 cascade로
   연결된 "slave"이며 포트 0xa0과 0xa1로 접근함.  포트 0x20에
   접근하면 A0 라인이 0으로, 0x21에 접근하면 A1 라인이 1로
   설정됨.  slave PIC도 상황이 비슷함.

   기본적으로 PIC가 전달하는 인터럽트 0...15는 인터럽트 벡터
   0...15로 가는데, 안타깝게도 이 벡터들은 CPU trap과
   exception에도 사용됨.  그래서 PIC를 재설정해 인터럽트
   0...15가 대신 인터럽트 벡터 32...47(0x20...0x2f)로
   전달되게 함. */

/* PIC를 초기화함.  자세한 내용은 [8259A] 참고. */
static void
pic_init (void) {
	/* 두 PIC의 모든 인터럽트를 mask함. */
	outb (0x21, 0xff);
	outb (0xa1, 0xff);

	/* master를 초기화함. */
	outb (0x20, 0x11); /* ICW1: single 모드, edge triggered, ICW4 필요. */
	outb (0x21, 0x20); /* ICW2: IR0...7 라인 -> irq 0x20...0x27. */
	outb (0x21, 0x04); /* ICW3: IR2 라인에 slave PIC가 연결됨. */
	outb (0x21, 0x01); /* ICW4: 8086 모드, normal EOI, non-buffered. */

	/* slave를 초기화함. */
	outb (0xa0, 0x11); /* ICW1: single 모드, edge triggered, ICW4 필요. */
	outb (0xa1, 0x28); /* ICW2: IR0...7 라인 -> irq 0x28...0x2f. */
	outb (0xa1, 0x02); /* ICW3: slave ID는 2. */
	outb (0xa1, 0x01); /* ICW4: 8086 모드, normal EOI, non-buffered. */

	/* 모든 인터럽트를 unmask함. */
	outb (0x21, 0x00);
	outb (0xa1, 0x00);
}

/* 주어진 IRQ에 대해 PIC에 end-of-interrupt 신호를 보냄.
   IRQ를 acknowledge하지 않으면 다시는 전달되지 않으므로
   이 동작은 중요함. */
static void
pic_end_of_interrupt (int irq) {
	ASSERT (irq >= 0x20 && irq < 0x30);

	/* master PIC를 acknowledge함. */
	outb (0x20, 0x20);

	/* slave 인터럽트라면 slave PIC도 acknowledge함. */
	if (irq >= 0x28)
		outb (0xa0, 0x20);
}
/* 인터럽트 핸들러. */

/* 모든 인터럽트, fault, exception을 처리하는 핸들러.  이 함수는
   intr-stubs.S의 어셈블리어 인터럽트 stub이 호출함.  FRAME은
   인터럽트와 인터럽트된 스레드의 레지스터를 나타냄. */
void
intr_handler (struct intr_frame *frame) {
	bool external;
	intr_handler_func *handler;

	/* 외부 인터럽트는 특별함.
	   한 번에 하나만 처리하며(따라서 인터럽트는 꺼져 있어야 함)
	   PIC에 acknowledge해야 함(아래 참고).
	   외부 인터럽트 핸들러는 sleep할 수 없음. */
	external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (!intr_context ());

		in_external_intr = true;
		yield_on_return = false;
	}

	/* 해당 인터럽트의 핸들러를 호출함. */
	handler = intr_handlers[frame->vec_no];
	if (handler != NULL)
		handler (frame);
	else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f) {
		/* 핸들러는 없지만, 이 인터럽트는 하드웨어 fault나 하드웨어
		   race condition 때문에 spurious하게(가짜로) 발생할 수 있음.
		   무시함. */
	} else {
		/* 핸들러도 없고 spurious도 아님.  예상치 못한 인터럽트
		   핸들러를 호출함. */
		intr_dump_frame (frame);
		PANIC ("Unexpected interrupt");
	}

	/* 외부 인터럽트 처리를 마무리함. */
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (intr_context ());

		in_external_intr = false;
		pic_end_of_interrupt (frame->vec_no);

		if (yield_on_return)
			thread_yield ();
	}
}

/* 디버깅을 위해 인터럽트 프레임 F를 콘솔에 덤프함. */
void
intr_dump_frame (const struct intr_frame *f) {
	/* CR2는 마지막 page fault의 선형 주소(linear address)임.
	   [IA32-v2a] "MOV--Move to/from Control Registers"와
	   [IA32-v3a] 5.14 "Interrupt 14--Page Fault Exception
	   (#PF)" 참고. */
	uint64_t cr2 = rcr2();
	printf ("Interrupt %#04llx (%s) at rip=%llx\n",
			f->vec_no, intr_names[f->vec_no], f->rip);
	printf (" cr2=%016llx error=%16llx\n", cr2, f->error_code);
	printf ("rax %016llx rbx %016llx rcx %016llx rdx %016llx\n",
			f->R.rax, f->R.rbx, f->R.rcx, f->R.rdx);
	printf ("rsp %016llx rbp %016llx rsi %016llx rdi %016llx\n",
			f->rsp, f->R.rbp, f->R.rsi, f->R.rdi);
	printf ("rip %016llx r8 %016llx  r9 %016llx r10 %016llx\n",
			f->rip, f->R.r8, f->R.r9, f->R.r10);
	printf ("r11 %016llx r12 %016llx r13 %016llx r14 %016llx\n",
			f->R.r11, f->R.r12, f->R.r13, f->R.r14);
	printf ("r15 %016llx rflags %08llx\n", f->R.r15, f->eflags);
	printf ("es: %04x ds: %04x cs: %04x ss: %04x\n",
			f->es, f->ds, f->cs, f->ss);
}

/* 인터럽트 VEC의 이름을 반환함. */
const char *
intr_name (uint8_t vec) {
	return intr_names[vec];
}
