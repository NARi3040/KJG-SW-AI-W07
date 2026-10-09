#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/**
 * @brief counting 세마포어
 *
 * @details 
 * 음이 아닌 정수 value와, value가 0이라 기다리는 스레드들의 목록 waiters로 구성됨.
 * sema_down()은 value가 양수가 될 때까지 기다린 뒤 1 감소시키고,
 * sema_up()은 value를 1 증가시키며 waiters의 스레드 하나를 깨움.
 * 
 * @note value 이해 하기 어려우면 그냥 주차장 생각하셈.
 * @note waiters에는 스레드의 elem이 들어감. BLOCKED 스레드는 ready_list에 없으므로 elem을 공유함.
 * @note waiters는 list_push_back()으로 넣고 list_pop_front()로 꺼내는 FIFO임.
 */
struct semaphore {
	unsigned value;             /* 현재 값. */
	struct list waiters;        /* 대기 중인 스레드 목록. */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* 락(Lock). */
struct lock {
	struct thread *holder;      /* 락을 보유한 스레드 (디버깅용). */
	struct semaphore semaphore; /* 접근을 제어하는 binary 세마포어. */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* 조건 변수(Condition variable). */
struct condition {
	struct list waiters;        /* 대기 중인 스레드 목록. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* 최적화 배리어(Optimization barrier).
 *
 * 컴파일러는 최적화 배리어를 가로질러 연산 순서를 재배치하지
 * 않음.  자세한 내용은 레퍼런스 가이드의 "Optimization Barriers"
 * 참고. */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
