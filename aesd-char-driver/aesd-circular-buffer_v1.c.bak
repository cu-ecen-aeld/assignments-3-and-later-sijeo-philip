/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
	uint8_t i;
	uint8_t index;
	uint8_t count = 0;

	/*Determine how many valid enteries are in the buffer. 
	 * If full, the bufffer contains the maximum number of enteries
	 * Otherwise, the numbe oof enteries is base on the relative position.
	 */
	if (buffer->full) {
		count = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	}else if (buffer->in_offs >= buffer->out_offs) {
		count = buffer->in_offs- buffer->out_offs;
	}else {
		count = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs + buffer->in_offs;
	}
	/*Iterate over the valid enteries in their logical order.
	 * Use modulo airthematic to wrap around the circular array.
	 * For each entry, check if the current entry contains the char_offset.
	 */
	for (i = 0; i < count; i++) {
		index = (buffer->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
		if (char_offset < buffer->entry[index].size) {
			/*The offset falls within the entry*/
			*entry_offset_byte_rtn = char_offset;
			return &buffer->entry[index];
		}
		char_offset -= buffer->entry[index].size;
	}

	
	/*If no entry is found then return NULL pointer */
    	return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
	/*Copy the new entry into the buffer at the position specified by in_offs*/
	buffer->entry[buffer->in_offs] = *add_entry;
	/*if the buffer is already full, we are overwriting the old entry Therefore, advance the out_offs to point to the next oldest entry
	 */
	if (buffer->full) {
		buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	}

	/*Advance the in_offs to the next position */
	buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

	/*if in_offs was wrapped around and equals out_offs, the buffer is full */
	if (buffer->out_offs == buffer->in_offs){
		buffer->full = true;
	}else {
		buffer->full = false;
	}

}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
