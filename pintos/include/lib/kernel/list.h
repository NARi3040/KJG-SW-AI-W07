#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* Doubly linked list.
 *
 * This implementation of a doubly linked list does not require
 * use of dynamically allocated memory.  Instead, each structure
 * that is a potential list element must embed a struct list_elem
 * member.  All of the list functions operate on these `struct
 * list_elem's.  The list_entry macro allows conversion from a
 * struct list_elem back to a structure object that contains it.

 * For example, suppose there is a needed for a list of `struct
 * foo'.  `struct foo' should contain a `struct list_elem'
 * member, like so:

 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...other members...
 * };

 * Then a list of `struct foo' can be be declared and initialized
 * like so:

 * struct list foo_list;

 * list_init (&foo_list);

 * Iteration is a typical situation where it is necessary to
 * convert from a struct list_elem back to its enclosing
 * structure.  Here's an example using foo_list:

 * struct list_elem *e;

 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 * e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...do something with f...
 * }

 * You can find real examples of list usage throughout the
 * source; for example, malloc.c, palloc.c, and thread.c in the
 * threads directory all use lists.

 * The interface for this list is inspired by the list<> template
 * in the C++ STL.  If you're familiar with list<>, you should
 * find this easy to use.  However, it should be emphasized that
 * these lists do *no* type checking and can't do much other
 * correctness checking.  If you screw up, it will bite you.

 * Glossary of list terms:

 * - "front": The first element in a list.  Undefined in an
 * empty list.  Returned by list_front().

 * - "back": The last element in a list.  Undefined in an empty
 * list.  Returned by list_back().

 * - "tail": The element figuratively just after the last
 * element of a list.  Well defined even in an empty list.
 * Returned by list_end().  Used as the end sentinel for an
 * iteration from front to back.

 * - "beginning": In a non-empty list, the front.  In an empty
 * list, the tail.  Returned by list_begin().  Used as the
 * starting point for an iteration from front to back.

 * - "head": The element figuratively just before the first
 * element of a list.  Well defined even in an empty list.
 * Returned by list_rend().  Used as the end sentinel for an
 * iteration from back to front.

 * - "reverse beginning": In a non-empty list, the back.  In an
 * empty list, the head.  Returned by list_rbegin().  Used as
 * the starting point for an iteration from back to front.
 *
 * - "interior element": An element that is not the head or
 * tail, that is, a real list element.  An empty list does
 * not have any interior elements.*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 이중 연결 리스트의 노드임.
 *
 * 데이터는 없고 prev/next 포인터만 가짐. 리스트에 넣을 구조체 안에
 * 멤버로 심어서 쓰는 intrusive 방식임. (예: struct thread의 elem)
 * 노드 포인터에서 원래 구조체를 복원하려면 list_entry()를 사용함.
 *
 * @see list_entry
 */
struct list_elem {
	struct list_elem *prev;     /**< 이전 노드. */
	struct list_elem *next;     /**< 다음 노드. */
};

/* List. */
struct list {
	struct list_elem head;      /* List head. */
	struct list_elem tail;      /* List tail. */
};

/**
 * @brief list_elem 포인터로부터 그 노드를 멤버로 가진 바깥 구조체의 포인터를 복원함.
 *
 * @details
 * &LIST_ELEM->next(노드 시작 주소 + 8바이트)에서 offsetof(STRUCT, MEMBER.next)를
 * 빼서 구조체의 시작 주소를 구함. MEMBER.next는 구조체 안에서 elem 멤버의
 * next 필드까지의 오프셋임. 둘 다 next 기준이라 elem 기준으로 계산한 것과 결과가 같음.
 *
 * @verbatim
 * struct thread (메모리)
 * +------------------+ <- t        (우리가 원하는 주소)
 * | tid, name, ...   |
 * | ...              |
 * +------------------+ <- &t->elem (= e, 리스트가 가진 주소)
 * | elem (prev,next) |
 * +------------------+
 * | wakeup_tick ...  |
 * +------------------+
 * @endverbatim
 *
 * @param LIST_ELEM 구조체에 심어 둔 struct list_elem을 가리키는 포인터.
 * @param STRUCT    바깥 구조체의 타입 이름. (예: struct thread)
 * @param MEMBER    STRUCT 안에서 list_elem 멤버의 이름. (예: elem)
 *
 * @return STRUCT * 타입의 포인터.
 *
 * @note LIST_ELEM이 실제로 STRUCT의 MEMBER 멤버를 가리킬 때만 유효함.
 *       다른 곳을 가리키면 잘못된 주소가 나오고 검사도 없음.
 * @note 사용 예: list_entry (e, struct thread, elem)
 * @note 이딴게 왜 있지
 *
 * @see struct list_elem
 */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))

void list_init (struct list *);

/* List traversal. */
struct list_elem *list_begin (struct list *);
struct list_elem *list_next (struct list_elem *);
struct list_elem *list_end (struct list *);

struct list_elem *list_rbegin (struct list *);
struct list_elem *list_prev (struct list_elem *);
struct list_elem *list_rend (struct list *);

struct list_elem *list_head (struct list *);
struct list_elem *list_tail (struct list *);

/* List insertion. */
void list_insert (struct list_elem *, struct list_elem *);
void list_splice (struct list_elem *before, struct list_elem *first, struct list_elem *last);
void list_push_front (struct list *, struct list_elem *);
void list_push_back (struct list *, struct list_elem *);

/* List removal. */
struct list_elem *list_remove (struct list_elem *);
struct list_elem *list_pop_front (struct list *);
struct list_elem *list_pop_back (struct list *);

/* List elements. */
struct list_elem *list_front (struct list *);
struct list_elem *list_back (struct list *);

/* List properties. */
size_t list_size (struct list *);
bool list_empty (struct list *); // ?

/* Miscellaneous. */
void list_reverse (struct list *);

/* Compares the value of two list elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

/* Operations on lists with ordered elements. */
void list_sort (struct list *,
                list_less_func *, void *aux);
void list_insert_ordered (struct list *, struct list_elem *,
                          list_less_func *, void *aux);
void list_unique (struct list *, struct list *duplicates,
                  list_less_func *, void *aux);

/* Max and min. */
struct list_elem *list_max (struct list *, list_less_func *, void *aux);
struct list_elem *list_min (struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
