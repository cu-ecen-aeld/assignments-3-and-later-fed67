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
    /**
    * TODO: implement per description
    */
   //No data has been written
   if ( ((buffer->out_offs - buffer->in_offs) == 0)  && !buffer->full) {
    return NULL;
   }

   size_t current = buffer->out_offs;
   size_t offset_element = 0;
   while (1) {
        if ( (offset_element+buffer->entry[current].size) > char_offset) {
            offset_element = char_offset - offset_element;
            break;
        }
        offset_element += buffer->entry[current].size;


        current = ((current+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
        if(current == buffer->in_offs) {
            return NULL;
        }
   }

   *entry_offset_byte_rtn = offset_element;

//    *entry_offset_byte_rtn = buffer->entry[current].size;
   return &(buffer->entry[current]);
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
    /**
    * TODO: implement per description
    */
   buffer->entry[buffer->in_offs] = *add_entry;

    if( buffer->full) {
        buffer->in_offs = (buffer->in_offs+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        buffer->out_offs = (buffer->out_offs+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else {
        buffer->in_offs = (buffer->in_offs+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        if( buffer->in_offs  == buffer->out_offs) {
            buffer->full = true;
        }
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}


size_t get_total_size(struct aesd_circular_buffer *buffer)
{   
    size_t size = 0;
    if( buffer->full) {
        //iterate over all elements 
        for(size_t i = 0; i != AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i) {
            size += buffer->entry[i].size;
        }
    } else {
        uint8_t idx = buffer->out_offs;
        // iterate foreward
        while(idx != buffer->in_offs) {
            size += buffer->entry[idx].size;
            idx = (idx+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; 
        }
    }
    return size;
}

int get_positions(struct aesd_circular_buffer *buffer, struct pair position)
{   
    int size = 0;
    // printk("position i %i j %i full %i in_off %i \n", position.i, position.j, buffer->full, buffer->in_offs );
    if( buffer->full && position.i >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        printk("return if \n");
        //iterate over all elements 
        return -1;        
    } else if( !buffer->full && position.i >= buffer->in_offs) {
        printk("return else if \n");
        return -1;
    }

    for(uint32_t i = 0; i < position.i; ++i) {
        printk("for i %i \n", i);
        size += buffer->entry[i].size;
    }
    
    size += position.j;
    return size;
}
