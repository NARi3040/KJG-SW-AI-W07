#include "list.h"
#include "../debug.h"

/* 우리의 doubly linked list는 두 개의 header 요소를 가짐:
   첫 번째 요소 바로 앞의 "head"와 마지막 요소 바로 뒤의
   "tail"임.  앞쪽 header의 `prev' 링크는 null이고, 뒤쪽
   header의 `next' 링크도 마찬가지임.  나머지 두 링크는
   리스트 내부 요소들을 거쳐 서로를 가리킴.

   빈 리스트는 다음과 같이 생김:

    +------+     +------+
<---| head |<--->| tail |--->
    +------+     +------+

   요소가 두 개인 리스트는 다음과 같이 생김:

    +------+     +-------+     +-------+     +------+
<---| head |<--->|   1   |<--->|   2   |<--->| tail |--->
    +------+     +-------+     +-------+     +------+

   이 대칭 구조 덕분에 리스트 처리에서 많은 특수 케이스가 사라짐.
   예를 들어 list_remove()를 보면, 포인터 대입 두 번만 필요하고
   조건문이 없음.  header 요소가 없었다면 코드가 훨씬 복잡했을 것임.

   (각 header 요소에서는 포인터 중 하나만 사용되므로, 사실 이 둘을
   하나의 header 요소로 합쳐도 이 단순함을 잃지 않음.  하지만 두 개의
   별도 요소를 쓰면 일부 연산에서 약간의 검사를 할 수 있어서
   유용할 수 있음.) */

static bool is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) UNUSED;

/* ELEM이 head이면 true, 아니면 false를 반환함. */
static inline bool
is_head (struct list_elem *elem) {
	return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* ELEM이 내부(interior) 요소이면 true,
   아니면 false를 반환함. */
static inline bool
is_interior (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* ELEM이 tail이면 true, 아니면 false를 반환함. */
static inline bool
is_tail (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* LIST를 빈 리스트로 초기화함. */
void
list_init (struct list *list) {
	ASSERT (list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* LIST의 시작(첫 요소)을 반환함. */
struct list_elem *
list_begin (struct list *list) {
	ASSERT (list != NULL);
	return list->head.next;
}

/* ELEM이 속한 리스트에서 ELEM 다음 요소를 반환함.  ELEM이
   리스트의 마지막 요소라면 리스트 tail을 반환함.  ELEM 자신이
   리스트 tail이면 결과는 정의되지 않음(undefined). */
struct list_elem *
list_next (struct list_elem *elem) {
	ASSERT (is_head (elem) || is_interior (elem));
	return elem->next;
}

/* LIST의 tail을 반환함.

list_end()는 리스트를 앞에서 뒤로 순회할 때 자주 쓰임.
예시는 list.h 상단의 큰 주석 참고. */
struct list_elem *
list_end (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* LIST를 뒤에서 앞으로 역순 순회하기 위한, LIST의
   reverse beginning을 반환함. */
struct list_elem *
list_rbegin (struct list *list) {
	ASSERT (list != NULL);
	return list->tail.prev;
}

/* ELEM이 속한 리스트에서 ELEM 이전 요소를 반환함.  ELEM이
   리스트의 첫 요소라면 리스트 head를 반환함.  ELEM 자신이
   리스트 head이면 결과는 정의되지 않음(undefined). */
struct list_elem *
list_prev (struct list_elem *elem) {
	ASSERT (is_interior (elem) || is_tail (elem));
	return elem->prev;
}

/* LIST의 head를 반환함.

list_rend()는 리스트를 뒤에서 앞으로 역순 순회할 때 자주 쓰임.
list.h 상단의 예시를 따른 전형적인 사용법은 다음과 같음:

   for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
   e = list_prev (e))
   {
   struct foo *f = list_entry (e, struct foo, elem);
   ...do something with f...
   }
   */
struct list_elem *
list_rend (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 head를 반환함.

list_head()는 리스트를 순회하는 다른 방식에 쓸 수 있음. 예:

   e = list_head (&list);
   while ((e = list_next (e)) != list_end (&list))
   {
   ...
   }
   */
struct list_elem *
list_head (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 tail을 반환함. */
struct list_elem *
list_tail (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* ELEM을 BEFORE 바로 앞에 삽입함.  BEFORE는 내부 요소이거나
   tail일 수 있음.  후자는 list_push_back()과 동일함. */
void
list_insert (struct list_elem *before, struct list_elem *elem) {
	ASSERT (is_interior (before) || is_tail (before));
	ASSERT (elem != NULL);

	elem->prev = before->prev;
	elem->next = before;
	before->prev->next = elem;
	before->prev = elem;
}

/* FIRST부터 LAST 직전까지(LAST 제외)의 요소들을 현재 리스트에서
   제거한 뒤, BEFORE 바로 앞에 삽입함.  BEFORE는 내부 요소이거나
   tail일 수 있음. */
void
list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last) {
	ASSERT (is_interior (before) || is_tail (before));
	if (first == last)
		return;
	last = list_prev (last);

	ASSERT (is_interior (first));
	ASSERT (is_interior (last));

	/* FIRST...LAST를 현재 리스트에서 깔끔하게 제거함. */
	first->prev->next = last->next;
	last->next->prev = first->prev;

	/* FIRST...LAST를 새 리스트에 이어 붙임(splice). */
	first->prev = before->prev;
	last->next = before;
	before->prev->next = first;
	before->prev = last;
}

/* ELEM을 LIST의 맨 앞에 삽입해 LIST의 front가 되게 함. */
void
list_push_front (struct list *list, struct list_elem *elem) {
	list_insert (list_begin (list), elem);
}

/* ELEM을 LIST의 맨 끝에 삽입해 LIST의 back이 되게 함. */
void
list_push_back (struct list *list, struct list_elem *elem) {
	list_insert (list_end (list), elem);
}

/* ELEM을 자신이 속한 리스트에서 제거하고, 그 다음에 있던 요소를
   반환함.  ELEM이 리스트에 없으면 동작이 정의되지 않음.

   제거한 뒤에는 ELEM을 리스트의 요소로 취급하는 것이 안전하지
   않음.  특히 제거 후 ELEM에 list_next()나 list_prev()를 쓰면
   동작이 정의되지 않음.  따라서 리스트의 요소를 제거하는 단순한
   루프는 실패함:

 ** 이렇게 하지 말 것 **
 for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 {
 ...do something with e...
 list_remove (e);
 }
 ** 이렇게 하지 말 것 **

 리스트에서 요소를 순회하며 제거하는 올바른 방법 중 하나는
다음과 같음:

for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
{
...do something with e...
}

리스트의 요소를 free()해야 한다면 더 보수적으로 접근해야 함.
그 경우에도 동작하는 다른 방법은 다음과 같음:

while (!list_empty (&list))
{
struct list_elem *e = list_pop_front (&list);
...do something with e...
}
*/
struct list_elem *
list_remove (struct list_elem *elem) {
	ASSERT (is_interior (elem));
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	return elem->next;
}

/* LIST의 front 요소를 제거하고 반환함.
   제거 전에 LIST가 비어 있으면 동작이 정의되지 않음. */
struct list_elem *
list_pop_front (struct list *list) {
	struct list_elem *front = list_front (list);
	list_remove (front);
	return front;
}

/* LIST의 back 요소를 제거하고 반환함.
   제거 전에 LIST가 비어 있으면 동작이 정의되지 않음. */
struct list_elem *
list_pop_back (struct list *list) {
	struct list_elem *back = list_back (list);
	list_remove (back);
	return back;
}

/* LIST의 front 요소를 반환함.
   LIST가 비어 있으면 동작이 정의되지 않음. */
struct list_elem *
list_front (struct list *list) {
	ASSERT (!list_empty (list));
	return list->head.next;
}

/* LIST의 back 요소를 반환함.
   LIST가 비어 있으면 동작이 정의되지 않음. */
struct list_elem *
list_back (struct list *list) {
	ASSERT (!list_empty (list));
	return list->tail.prev;
}

/* LIST의 요소 개수를 반환함.
   요소 수 n에 대해 O(n)으로 실행됨. */
size_t
list_size (struct list *list) {
	struct list_elem *e;
	size_t cnt = 0;

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		cnt++;
	return cnt;
}

/* LIST가 비어 있으면 true, 아니면 false를 반환함. */
bool
list_empty (struct list *list) {
	return list_begin (list) == list_end (list);
}

/* A와 B가 가리키는 `struct list_elem *'를 서로 바꿈. */
static void
swap (struct list_elem **a, struct list_elem **b) {
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* LIST의 순서를 뒤집음. */
void
list_reverse (struct list *list) {
	if (!list_empty (list)) {
		struct list_elem *e;

		for (e = list_begin (list); e != list_end (list); e = e->prev)
			swap (&e->prev, &e->next);
		swap (&list->head.next, &list->tail.prev);
		swap (&list->head.next->prev, &list->tail.prev->next);
	}
}

/* 리스트 요소 A부터 B 직전까지(B 제외)가 보조 데이터 AUX를 받는
   LESS 기준으로 정렬되어 있을 때만 true를 반환함. */
static bool
is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	if (a != b)
		while ((a = list_next (a)) != b)
			if (less (a, list_prev (a), aux))
				return false;
	return true;
}

/* A에서 시작해 B를 넘지 않는 곳에서 끝나는, 보조 데이터 AUX를 받는
   LESS 기준으로 nondecreasing 순서인 리스트 요소들의 run을 찾음.
   run의 (exclusive) 끝을 반환함.
   A부터 B 직전까지(B 제외)는 비어 있지 않은 범위여야 함. */
static struct list_elem *
find_end_of_run (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	ASSERT (a != NULL);
	ASSERT (b != NULL);
	ASSERT (less != NULL);
	ASSERT (a != b);

	do {
		a = list_next (a);
	} while (a != b && !less (a, list_prev (a), aux));
	return a;
}

/* A0부터 A1B0 직전까지(A1B0 제외)와 A1B0부터 B1 직전까지(B1 제외)를
   병합해, 마찬가지로 B1(제외)에서 끝나는 하나의 범위를 만듦.  두
   입력 범위는 모두 비어 있지 않아야 하고, 보조 데이터 AUX를 받는
   LESS 기준으로 nondecreasing 순서로 정렬되어 있어야 함.  출력
   범위도 같은 방식으로 정렬됨. */
static void
inplace_merge (struct list_elem *a0, struct list_elem *a1b0,
		struct list_elem *b1,
		list_less_func *less, void *aux) {
	ASSERT (a0 != NULL);
	ASSERT (a1b0 != NULL);
	ASSERT (b1 != NULL);
	ASSERT (less != NULL);
	ASSERT (is_sorted (a0, a1b0, less, aux));
	ASSERT (is_sorted (a1b0, b1, less, aux));

	while (a0 != a1b0 && a1b0 != b1)
		if (!less (a1b0, a0, aux))
			a0 = list_next (a0);
		else {
			a1b0 = list_next (a1b0);
			list_splice (a0, list_prev (a1b0), a1b0);
		}
}

/* 보조 데이터 AUX를 받는 LESS 기준으로 LIST를 정렬함.  자연(natural)
   iterative merge sort를 사용하며, LIST의 요소 수에 대해
   O(n lg n) 시간, O(1) 공간으로 실행됨. */
void
list_sort (struct list *list, list_less_func *less, void *aux) {
	size_t output_run_cnt;        /* 현재 pass에서 출력한 run의 개수. */

	ASSERT (list != NULL);
	ASSERT (less != NULL);

	/* 리스트를 반복해서 훑으며, nondecreasing 요소들의 인접한 run을
	   병합함.  run이 하나만 남을 때까지 반복함. */
	do {
		struct list_elem *a0;     /* 첫 번째 run의 시작. */
		struct list_elem *a1b0;   /* 첫 번째 run의 끝, 두 번째 run의 시작. */
		struct list_elem *b1;     /* 두 번째 run의 끝. */

		output_run_cnt = 0;
		for (a0 = list_begin (list); a0 != list_end (list); a0 = b1) {
			/* 반복마다 출력 run이 하나씩 만들어짐. */
			output_run_cnt++;

			/* nondecreasing 요소들의 인접한 run 두 개
			   A0...A1B0과 A1B0...B1을 찾음. */
			a1b0 = find_end_of_run (a0, list_end (list), less, aux);
			if (a1b0 == list_end (list))
				break;
			b1 = find_end_of_run (a1b0, list_end (list), less, aux);

			/* run들을 병합함. */
			inplace_merge (a0, a1b0, b1, less, aux);
		}
	}
	while (output_run_cnt > 1);

	ASSERT (is_sorted (list_begin (list), list_end (list), less, aux));
}

/* 보조 데이터 AUX를 받는 LESS 기준으로 정렬되어 있어야 하는 LIST의
   알맞은 위치에 ELEM을 삽입함.
   LIST의 요소 수에 대해 평균적으로 O(n)에 실행됨. */
void
list_insert_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux) {
	struct list_elem *e;

	ASSERT (list != NULL);
	ASSERT (elem != NULL);
	ASSERT (less != NULL);

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux))
			break;
	return list_insert (e, elem);
}

/* LIST를 순회하며, 보조 데이터 AUX를 받는 LESS 기준으로 서로 같은
   인접 요소 묶음마다 첫 번째를 제외한 나머지를 모두 제거함.
   DUPLICATES가 null이 아니면, LIST에서 제거된 요소들을 DUPLICATES에
   추가함. */
void
list_unique (struct list *list, struct list *duplicates,
		list_less_func *less, void *aux) {
	struct list_elem *elem, *next;

	ASSERT (list != NULL);
	ASSERT (less != NULL);
	if (list_empty (list))
		return;

	elem = list_begin (list);
	while ((next = list_next (elem)) != list_end (list))
		if (!less (elem, next, aux) && !less (next, elem, aux)) {
			list_remove (next);
			if (duplicates != NULL)
				list_push_back (duplicates, next);
		} else
			elem = next;
}

/* 보조 데이터 AUX를 받는 LESS 기준으로 LIST에서 가장 큰 값을 가진
   요소를 반환함.  최댓값이 여러 개면 리스트에서 더 앞에 나오는 것을
   반환함.  리스트가 비어 있으면 tail을 반환함. */
struct list_elem *
list_max (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *max = list_begin (list);
	if (max != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (max); e != list_end (list); e = list_next (e))
			if (less (max, e, aux))
				max = e;
	}
	return max;
}

/* 보조 데이터 AUX를 받는 LESS 기준으로 LIST에서 가장 작은 값을 가진
   요소를 반환함.  최솟값이 여러 개면 리스트에서 더 앞에 나오는 것을
   반환함.  리스트가 비어 있으면 tail을 반환함. */
struct list_elem *
list_min (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *min = list_begin (list);
	if (min != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (min); e != list_end (list); e = list_next (e))
			if (less (e, min, aux))
				min = e;
	}
	return min;
}
