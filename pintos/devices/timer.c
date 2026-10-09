/**
 * @file timer.c 
 * @brief 8254 PIT(Programmable Interval Timer) 기반 타이머 구현.
 * @date 2026-10-08
 */

#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 세부 사항은 [8254] 참고. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후 지난 타이머 틱 수. */
static int64_t ticks;

/* 타이머 틱 하나당 루프 횟수.
   timer_calibrate()가 초기화함. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/**
 * @brief 8254 PIT(Programmable Interval Timer)가 초당 PIT_FREQ번 인터럽트를 발생시키도록 설정하고, 해당 인터럽트를 등록함
 * 
 * 
 * @note 아직 뭔지 모르곘음
 */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값.
	   가장 가까운 정수로 반올림. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: 카운터 0, LSB 다음 MSB, 모드 2, 바이너리. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}


/**
 * @brief 짧은 지연을 구현하는 데 쓰이는 loops_per_tick을 보정(calibrate)함.
 * 
 * 
 * @note 아직 뭔지 모르곘음
 */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* loops_per_tick을 한 타이머 틱보다 작은 가장 큰
	   2의 거듭제곱으로 근사함. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick의 다음 8비트를 정밀하게 조정함. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}


/**
 * @brief OS 부팅 이후 지금까지 흐른 타이머 틱 수를 반환함
 *
 * 전역 변수 ticks의 현재 값을 읽어서 돌려줌. 읽는 동안 인터럽트를 꺼서
 * 타이머 인터럽트 핸들러가 ticks를 갱신하는 도중에 읽는 상황을 막음.
 *
 * @return 부팅 이후 경과한 타이머 틱 수 (TIMER_FREQ 틱 = 1초)
 *
 * @note 읽은 뒤 barrier()로 컴파일러가 이 읽기를 뒤로 재배치하지 못하게 막음.
 *
 * @see timer_elapsed(), timer_sleep()
 */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/**
 * @brief timer_ticks()가 이전에 반환했던 값인 과거 특정 시점(then) 이후로 경과한 타이머 틱의 수를 반환함
 * 
 * @param[in] then 함수를 호출한 특정 시점의 틱 수 
 * 
 * @return 과거 특정 시점(then)부터 현재까지 경과한 타이머 틱(Timer Tick)의 개수를 반환함
 */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then; // 지금 틱과 들어온 틱을 뺴서 줌
}

/**
 * @brief 지정된 틱(ticks) 동안 호출 스레드의 실행을 일시 중지함
 *
 * @param[in] ticks 일시 중지할 타이머 틱(timer tick) 수
 *
 * @note 
 * 현재 기본 구현은 Busy Waiting(thread_yield 반복 호출) 방식이므로 CPU 자원을 낭비함. 
 * 스레드를 Block 상태로 재운 뒤 지정된 틱 이후 깨우도록 개선이 필요
 *
 * @see timer_ticks(), timer_elapsed(), thread_yield()
 */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks (); // 시작 시점의 tick 기록

	ASSERT (intr_get_level () == INTR_ON);
	while (timer_elapsed (start) < ticks) // 목표 tick이 지날 때까지 반복
		thread_yield (); // CPU 제어권을 다른 스레드에 양보 (Ready 상태로 전이)
}

/**
 * @brief 약 MS 밀리초 동안 실행을 일시 중지함.
 * 
 * 
 *
 * @param[in] ms 모름
 * @note 아직 뭔지 모르곘음
 */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/**
 * @brief 약 US 밀리초 동안 실행을 일시 중지함.
 * 
 * 
 *
 * @param[in] us 모름
 * @note 아직 뭔지 모르곘음
 */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/**
 * @brief 약 NS 밀리초 동안 실행을 일시 중지함.
 * 
 * 
 *
 * @param[in] ns 모름
 * @note 아직 뭔지 모르곘음
 */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/**
 * @brief 타이머 통계를 출력함.
 * 
 * 
 * @note 아직 뭔지 모르곘음
 */

void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/**
 * @brief 타이머 인터럽트 핸들러.
 * 
 * 
 * @note 아직 뭔지 모르곘음
 */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
}


/**
 * @brief LOOPS번 반복이 타이머 틱 하나보다 오래 걸리면 true, 아니면 false를 반환함.
 * 
 * @return LOOPS번 반복이 타이머 틱 하나보다 오래 걸리면 true, 아니면 false를 반환함.
 * @note 아직 뭔지 모르곘음
 */
static bool
too_many_loops (unsigned loops) {
	/* 타이머 틱이 바뀔 때까지 기다림. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS번 루프를 실행함. */
	start = ticks;
	busy_wait (loops);

	/* 틱 수가 바뀌었다면 너무 오래 반복한 것임. */
	barrier ();
	return start != ticks;
}

/**
 * @brief CPU를 계속 쓰면서 반복문으로 상태를 확인하며 기다리는 방식
 * 
 * 짧은 지연을 구현하기 위해 단순한 루프를 LOOPS번 반복함.
 * 코드 정렬(alignment)이 타이밍에 크게 영향을 줄 수 있으므로 NO_INLINE으로 표시함. 
 * 이 함수가 위치마다 다르게 인라인되면 결과를 예측하기 어려워짐.
 *
 * @note 
 */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/**
 * @brief 대략 NUM/DENOM초 동안 sleep함.
 *
 * NUM/DENOM초를 타이머 틱으로 변환(내림)한 뒤, 1 tick 이상이면
 * timer_sleep()으로, 1 tick 미만이면 busy_wait()로 기다림.
 *
 * @param[in] num 분수의 분자. 기다릴 시간의 수치임. (int64_t라서 곱셈 오버플로를 피함)
 * @param[in] denom 분수의  분모. num의 단위를 정함. 1초를 몇 등분한 단위인지를 뜻함.
 * @pre 인터럽트가 켜져 있어야 함 (intr_get_level() == INTR_ON).
 * @pre 1 tick 미만 대기(busy_wait 분기)에서는 denom이 1000의 배수여야 함.
 * @note 1 tick 미만 구간은 sleep/wakeup 방식이 적용되지 않고 항상 busy-wait임.
 *
 * @see timer_msleep(ms), timer_usleep(us), timer_nsleep(ns)
 */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM초를 타이머 틱으로 변환함 (내림).

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom; // 기다릴 타이머 틱 수임.

	ASSERT (intr_get_level () == INTR_ON);
	/*  */
	if (ticks > 0) {
		/* 최소 한 타이머 틱 이상 기다리는 경우임.
		   CPU를 다른 프로세스에 양보(yield)하는
		   timer_sleep()을 사용함. */
		timer_sleep (ticks); // 이게 busy wait 방식이래
	} else { // tick == 0 /
		/* 그 외에는 틱 미만 단위의 더 정확한 타이밍을 위해
		   busy-wait 루프를 사용함. 오버플로 가능성을 피하려고
		   분자와 분모를 1000으로 나눠 줄임. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); // tick 1보다 작으니 화장실 문 두두리기
	}
}
