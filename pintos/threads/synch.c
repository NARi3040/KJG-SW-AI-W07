/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 세마포어 SEMA를 VALUE로 초기화함.  세마포어는 음이 아닌
   정수와, 이를 조작하는 두 개의 atomic 연산으로 이루어짐:

   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤 1 감소시킴.

   - up 또는 "V": 값을 1 증가시킴 (그리고 기다리는 스레드가
   있다면 하나를 깨움). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 down 또는 "P" 연산.  SEMA의 값이 양수가
   될 때까지 기다린 뒤 atomic하게 1 감소시킴.

   이 함수는 sleep할 수 있으므로 인터럽트 핸들러 안에서 호출하면
   안 됨.  인터럽트가 비활성화된 상태에서 호출할 수는 있지만,
   sleep하게 되면 다음에 스케줄되는 스레드가 아마 인터럽트를
   다시 켤 것임. 이것이 sema_down 함수임. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 down 또는 "P" 연산이되, 세마포어가 아직
   0이 아닐 때만 수행함.  세마포어가 감소되었으면 true,
   아니면 false를 반환함.

   이 함수는 인터럽트 핸들러에서 호출해도 됨. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포어에 대한 up 또는 "V" 연산.  SEMA의 값을 증가시키고,
   SEMA를 기다리는 스레드가 있다면 그중 하나를 깨움.

   이 함수는 인터럽트 핸들러에서 호출해도 됨. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters))
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	sema->value++;
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 한 쌍의 스레드 사이에서 제어가 "ping-pong"하도록 만드는
   세마포어 self-test.  무슨 일이 일어나는지 보려면 printf()
   호출을 넣을 것. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()가 사용하는 스레드 함수. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* LOCK을 초기화함.  lock은 한 시점에 최대 한 스레드만 보유할
   수 있음.  이 lock은 "recursive"하지 않음.  즉, lock을 현재
   보유한 스레드가 같은 lock을 다시 acquire하려 하면 오류임.

   lock은 초기값이 1인 세마포어의 특수화(specialization)임.
   lock과 그런 세마포어의 차이는 두 가지임.  첫째, 세마포어는
   1보다 큰 값을 가질 수 있지만 lock은 한 시점에 한 스레드만
   소유할 수 있음.  둘째, 세마포어에는 owner가 없어서 한
   스레드가 "down"하고 다른 스레드가 "up"할 수 있지만, lock은
   같은 스레드가 acquire와 release를 모두 해야 함.  이런
   제약이 부담스럽다면, lock 대신 세마포어를 써야 한다는
   좋은 신호임. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* LOCK을 acquire하며, 필요하면 사용 가능해질 때까지 sleep함.
   현재 스레드가 이미 이 lock을 보유하고 있으면 안 됨.

   이 함수는 sleep할 수 있으므로 인터럽트 핸들러 안에서 호출하면
   안 됨.  인터럽트가 비활성화된 상태에서 호출할 수 있지만,
   sleep해야 하면 인터럽트가 다시 켜짐. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
}

/* LOCK을 acquire하려 시도해 성공하면 true, 실패하면 false를
   반환함.  현재 스레드가 이미 이 lock을 보유하고 있으면 안 됨.

   이 함수는 sleep하지 않으므로 인터럽트 핸들러 안에서
   호출해도 됨. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 현재 스레드가 소유한 LOCK을 release함.
   이것이 lock_release 함수임.

   인터럽트 핸들러는 lock을 acquire할 수 없으므로, 인터럽트
   핸들러 안에서 lock을 release하려는 것은 의미가 없음. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하고 있으면 true, 아니면 false를
   반환함.  (다른 스레드가 lock을 보유하고 있는지 검사하는
   것은 race가 생길 수 있으니 주의.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 리스트에 들어가는 세마포어 하나. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 요소. */
	struct semaphore semaphore;         /* 이 세마포어. */
};

/* 조건 변수 COND를 초기화함.  조건 변수는 한 코드 조각이 어떤
   조건을 signal하고, 협력하는 코드가 그 signal을 받아 대응할 수
   있게 해 줌. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 atomic하게 release하고, 다른 코드가 COND를 signal할
   때까지 기다림.  COND가 signal된 뒤 반환하기 전에 LOCK을 다시
   acquire함.  이 함수를 호출하기 전에 LOCK을 보유하고 있어야 함.

   이 함수가 구현하는 monitor는 "Hoare" 방식이 아니라 "Mesa"
   방식임.  즉, signal을 보내고 받는 것이 atomic한 연산이 아님.
   따라서 보통 호출자는 wait가 끝난 뒤 조건을 다시 확인해야
   하며, 필요하면 다시 기다려야 함.

   하나의 조건 변수는 단 하나의 lock과만 연결되지만, 하나의
   lock은 임의 개수의 조건 변수와 연결될 수 있음.  즉, lock에서
   조건 변수로의 일대다(one-to-many) 매핑이 존재함.

   이 함수는 sleep할 수 있으므로 인터럽트 핸들러 안에서 호출하면
   안 됨.  인터럽트가 비활성화된 상태에서 호출할 수 있지만,
   sleep해야 하면 인터럽트가 다시 켜짐. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* COND를 기다리는 스레드가 있다면 (LOCK으로 보호됨), 그중
   하나를 signal해서 wait에서 깨어나게 함.  이 함수를 호출하기
   전에 LOCK을 보유하고 있어야 함.

   인터럽트 핸들러는 lock을 acquire할 수 없으므로, 인터럽트
   핸들러 안에서 조건 변수를 signal하려는 것은 의미가 없음. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* COND를 기다리는 모든 스레드를 (있다면) 깨움 (LOCK으로 보호됨).
   이 함수를 호출하기 전에 LOCK을 보유하고 있어야 함.

   인터럽트 핸들러는 lock을 acquire할 수 없으므로, 인터럽트
   핸들러 안에서 조건 변수를 signal하려는 것은 의미가 없음. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
