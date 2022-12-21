
#ifndef _LINUX_LIST_H_
#define _LINUX_LIST_H_

#include_next <linux/list.h>


/**
 * list_for_each_entry_from_rcu - iterate over a list from current point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_node within the struct.
 *
 * Iterate over the tail of a list starting from a given position,
 * which must have been in the list when the RCU read lock was taken.
 * This would typically require either that you obtained the node from a
 * previous walk of the list in the same RCU read-side critical section, or
 * that you held some sort of non-RCU reference (such as a reference count)
 * to keep the node alive *and* in the list.
 *
 * This iterator is similar to list_for_each_entry_continue_rcu() except
 * this starts from the given position and that one starts from the position
 * after the given position.
 */
#define list_for_each_entry_from_rcu(pos, head, member)			\
	for (; &(pos)->member != (head);					\
		pos = list_entry_rcu(pos->member.next, typeof(*(pos)), member))

/**
 * list_entry_rcu - get the struct for this entry
 * @ptr:        the &struct list_head pointer.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the list_head within the struct.
 *
 * This primitive may safely run concurrently with the _rcu list-mutation
 * primitives such as list_add_rcu() as long as it's guarded by rcu_read_lock().
 */
#define list_entry_rcu(ptr, type, member) \
	container_of(READ_ONCE(ptr), type, member)

#endif
