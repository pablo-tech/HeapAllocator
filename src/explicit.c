#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "allocator.h"
#include "debug_break.h"


/**
 * Definitions
 */
#define BLOCK_USED_MASK             0b001     
#define BLOCK_SIZE_MASK             0b111     
#define BLOCK_HEADER_BYTES          8
#define MIN_PAYLOAD_BYTES           8
#define BLOCK_LINK_BYTES           16


/**
 * Heap global variables
 */
static void *segment_start;     // heap start
static size_t segment_size;     // heap size

static size_t bytes_used;       // heap bytes


/**
 * Establishes if the a pointer is within another
 * 
 * Argument
 *  - ptr1: one to abide per the bound
 *  - ptr2: the bound
 * 
 * Return: if first pointer is within bound set by the second
 */
bool within_bounds (void* ptr1, void* ptr2){
    
    if (ptr1 == NULL  || ptr2 == NULL) {
        return false;
    }
    
    return ptr1 < ptr2;
}


/**
 * Get address of the top of the heap, plus an optional offset
 * 
 * Argument
 *  - offset: how many bytes off the top
 * 
 * Returns:
 *  pointer to the top of the header, shifted by the given offset
 */
void* heap_top (size_t offset) {
    void* top = (char*) segment_start + bytes_used + offset;
    assert (top != NULL);
    return top;
} 


/**
 * Round a number to a the nearest multiple of another
 * This can be used to o keep alignment in memory allocation,
 * which is achieved when the multiple is a power of 2
 * 
 * Arguments
 *  sz: a number to round up
 *  mult: round up to the nearest multiple of this number
 * 
 * Returns:
 *  the nearest multiple
 * 
 */
size_t roundup (size_t sz, size_t mult) {
    return (sz + mult - 1) & ~(mult - 1);
}


/**
 * Heap allocation header 
 */
typedef struct {
    /**
    * Header encoding:
    * - higher 5 bytes represent the size of the block
    * - lower 3 bytes are not used to represent size. 
    *       Lowest bit used to represent free/used
    */
    unsigned long encoding;

} heap_header;



/**
 * Gets the size from a header
 * 
 * Argument
 *  - header: the header to get the size from
 * 
 * Returns: size from the header's encoding
 */
size_t header_payload_size (heap_header header) {
    return (size_t) (header.encoding & ~BLOCK_SIZE_MASK);
}


/**
 * Get wether the header's block is used
 * 
 * Argument
 *  - header: the header to get the use from
 * 
 * Returns: wether the block is used or not
 */
bool header_block_is_used (heap_header header) {
    return (bool) (header.encoding & BLOCK_USED_MASK);
} 


/**
 * Factory of headers 
 * 
 * Argument
 *  - requested_size: block size, not counting the header
 *  - is_used: whether the block is being used
 * 
 * Returns: header with encoded size and usage
 */
heap_header header_factory (size_t requested_size, bool is_used) {
    heap_header header;
    header.encoding = requested_size;
    if (is_used) {
        header.encoding |= BLOCK_USED_MASK;
    } else {
        header.encoding &= ~BLOCK_SIZE_MASK;
    }
    return header;
}


/**
 * Heap allocation node, to maintain a free node link list
 */ 
typedef struct {
    heap_header* prev_header;
    heap_header* next_header;
} heap_link;



/**
 * Explic linked list global variables
 */
heap_header* free_blocks_head_ptr;
heap_header* free_blocks_tail_ptr;

/**
 * Factory of links 
 * 
 * Argument
 *  - prev_header: link list pointer to the previous node
 *  - next_header: link list pointer to the next node
 * 
 * Returns: link object with pointers
 */
heap_link link_factory (heap_header* prev_header, heap_header* next_header) {
    heap_link link;
    link.prev_header = prev_header;
    link.next_header = next_header;
    return link;
}


/**
 * Computes the pointer to the link in a block
 * 
 * Argument
 *  - header_ptr: pointer to the header
 * 
 * Returns: pointer to the link
 */
heap_link* get_block_link_from_header (heap_header* header_ptr) {
    return (heap_link*) ((char*) header_ptr + BLOCK_HEADER_BYTES);
}


/**
 * Total bytest that are not payload in a heap block
 */
size_t block_overhead_bytes () {
    return BLOCK_HEADER_BYTES + BLOCK_LINK_BYTES;
}


/**
 * Computes the pointer to the payload in a block
 * 
 * Argument
 *  - header_ptr: pointer to the haeader
 * 
 * Returns: pointer to the payload
 */
void* get_block_payload_from_header (heap_header* header_ptr) {
    return (char*) header_ptr + block_overhead_bytes ();
}


/**
 * Get the pointer to the header, from the link pointer
 * 
 * Argument
 *  - link_ptr: pointer to the link in a heap block
 * 
 * Returns: pointer to the header in the same heap block
 */
heap_header* get_block_pointer_from_link (heap_link* link_ptr) {
    return (heap_header*) ((char*) link_ptr - BLOCK_HEADER_BYTES);
}


/**
 * Get the pointer to the header, from the payload poniter
 * 
 * Argument
 *  - payload_ptr: pointer to the payload in a heap block
 * 
 * Returns: pointer to the header in the same heap block
 */
heap_header* get_block_pointer_from_payload (void* payload_ptr) {
    return (heap_header*) ((char*) payload_ptr - block_overhead_bytes ());
}


/**
 * Get the next header in the heap, for any block
 * 
 * Argument
 *  - header_object: a heap header
 *  - header_ptr: the location of the header in memory
 * 
 * Returns: the location of the next header
 */
heap_header* get_next_implicit_header (heap_header header_object, 
                                       heap_header* header_ptr) {
    size_t size = header_payload_size (header_object);
    void* next_header = (char*) get_block_payload_from_header (header_ptr) + size;
    return (heap_header*) next_header;
}


/**
 * Calculates the address of the next header
 * 
 * Argument
 *  - header_ptr: address of a header
 *  - padded_block_bytes: size of the whole block that starts at header
 * 
 * Returns: poiiniter to the nextheader
 * 
 */
heap_header* get_next_block_header (heap_header* header_ptr, size_t block_bytes) {
    heap_header* next_ptr = (heap_header*) ((char*) header_ptr + block_bytes);
    return next_ptr;
}



/**
 * Get next free block from the current block's header
 * 
 * Argument
 *  - header_ptr pointer to the current block
 *  
 * Returns: pointer to the next free block
 */
heap_header* get_next_free_block_from_header (heap_header* header_ptr) {
    heap_link* link_ptr = get_block_link_from_header (header_ptr);
    return link_ptr->next_header;        
}


/**
 * Get previous free block from the current block's header
 * 
 * Argument
 *  - header_ptr pointer to the current block
 *  
 * Returns: pointer to the previous free block
 */
heap_header* get_prev_free_block_from_header (heap_header* header_ptr) {
    heap_link* link_ptr = get_block_link_from_header (header_ptr);
    return link_ptr->prev_header;        
}


/**
 * Set a block's next free node
 * 
 * Argument
 *  - header: 
 *  - next_header
 */
void set_next_free_node (heap_header* header_ptr, heap_header* next_header_ptr) {
    heap_link* link_ptr = get_block_link_from_header (header_ptr);   
    link_ptr->next_header = next_header_ptr;
}


/**
 * Set a block's previous free node
 * 
 * Argument
 *  - header: 
 *  - next_header
 */
void set_prev_free_node (heap_header* header_ptr, heap_header* prev_header_ptr) {
    heap_link* link_ptr = get_block_link_from_header (header_ptr);   
    link_ptr->prev_header = prev_header_ptr;
}


/**
 * Write a header at a location
 * 
 * Argument
 *  - header_ptr: location
 *  - header: to write
 */
void write_header (void* header_ptr, heap_header* header) {
    void* result = memcpy (header_ptr, header, BLOCK_HEADER_BYTES);
    assert (result != NULL); 
}


/**
 * Read a header from memory
 * 
 * Argument
 * - ptr: pointer to the header
 * 
 * Returns: header 
 */
void read_header (heap_header* header_object, void *header_ptr) {
    void* result = memcpy (header_object, header_ptr, BLOCK_HEADER_BYTES); 
    assert (result != NULL);
}


/**
 * Write a link at a location
 * 
 * Argument
 *  - header_ptr: location
 *  - header: to write
 */
void write_link (heap_header* header_ptr, heap_link* link) {

    heap_header header;
    read_header (&header, header_ptr);
    heap_link* link_ptr = get_block_link_from_header (header_ptr);

    void* result = memcpy (link_ptr, link, BLOCK_LINK_BYTES);
    assert (result != NULL); 
}


/**
 * Gets the size from a header
 * 
 * Argument
 *  - header_ptr: poiinter to the header to get the size from
 * 
 * Returns: size from the header's encoding
 */
size_t block_payload_size (heap_header* header_ptr) {
    heap_header header;
    read_header (&header, header_ptr);
    size_t size = header_payload_size (header);    
    return size;
}


/**
 * Reset the heap allocator to an empty initial state
 * 
 * Arguments:
 *  heap_start: starting address for the heap
 *  heap_size : total size for the heap
 * 
 * Returns: true if initialization was successful, or false otherwise
 */ 
bool myinit (void *heap_start, size_t heap_size) {

    // stats
    bytes_used = 0;

    // exception
    if (heap_size == 0) {
        return false;
    }

    // init 
    segment_size = heap_size;

    // exception
    if (heap_start == NULL) {
        return false;
    }
    
    // init
    segment_start = heap_start;
    free_blocks_head_ptr = NULL;
    free_blocks_tail_ptr = NULL;
    
    return true;
}


/**
 * Returns the size of the smallest possible block
 */
size_t min_block_size (){
    // header plus link, plus minimal payload
    return block_overhead_bytes () + MIN_PAYLOAD_BYTES;
}


/**
 * Returns the size of the smallest block to fit a request
 */ 
size_t min_requested_size (size_t bytes_requested) {
    return block_overhead_bytes () + bytes_requested;
}


/**
 * Validates that the requested size for a new allocation 
 *  is within granted heap bounds/rules
 * 
 * Arguments:
 *  - bytes_requested: size the caller wants to store
 * 
 * Returns: true/false if the bytes requested can be granted
 */ 
size_t valid_alloc (size_t bytes_requested) {
    
    if (bytes_requested == 0) {
        return 0;
    }
    
    size_t padded_block_bytes = 
        roundup (min_requested_size (bytes_requested), ALIGNMENT);

    if (bytes_requested > MAX_REQUEST_SIZE) {
        return 0;
    }
    
    if (padded_block_bytes + bytes_used > segment_size) {
        return 0;
    }
    
    return padded_block_bytes;
}


/**
 * Ccomputes the memory needed for the payload inside a heap bloc
 * 
 * Argument
 *  - padded_block_bytes: memory requested by the client
 */
size_t request_payload (size_t padded_block_bytes) {
    return padded_block_bytes - BLOCK_HEADER_BYTES - BLOCK_LINK_BYTES;
}


/**
 * Finds the location of an unused block meeting size criterion
 * 
 * Argument
 *  - requested_size: the amount of memory requested
 * 
 * Returns: pointer to the header insertion address 
 */
heap_header* find_free_block (size_t requested_size) {
    
    // heap
    size_t padded_block_bytes = valid_alloc (requested_size);
    size_t padded_payload_bytes = request_payload (padded_block_bytes);

    heap_header* curr_header_ptr = free_blocks_head_ptr; 
    if (curr_header_ptr == NULL) {
        return NULL;
    }

    // header
    heap_header header;
    heap_header* found_fit = NULL;
    size_t best_size = 0;

    while (curr_header_ptr != NULL) {

        // current
        read_header (&header, curr_header_ptr);
        size_t size = header_payload_size (header);
        bool is_used = header_block_is_used (header);
        
        if (!is_used && size >= padded_payload_bytes) {
            // no fit found yet, or candidate fit found
            if (best_size == 0 || size < best_size) { 
                // no fit found yet
                found_fit = curr_header_ptr; 
                best_size = size; 
            } 
        } 
        
        // next
        curr_header_ptr = get_next_free_block_from_header (curr_header_ptr); 
    }

    return found_fit;
}


/**
 * Returns the address of the previous free block
 * 
 * Argument
 *  - to_free_header_ptr: pointer to the block to free
 * 
 * Returns: address of the free block with highest address 
 *  lower than the pointer of the block to free 
 */
heap_header* precedent_free_block_in_list (heap_header* to_free_header_ptr) {
    
    heap_header* curr_ptr = free_blocks_head_ptr;
    heap_header* prev_ptr = NULL;
    
    while (curr_ptr != NULL && within_bounds(curr_ptr, to_free_header_ptr)) {
        prev_ptr = curr_ptr;
        curr_ptr = get_next_free_block_from_header (curr_ptr);
    }
    
    return prev_ptr;
}


/**
 * Returns the address of the next free block
 * 
 * Argument
 *  - to_free_header_ptr: pointer to the block to free
 * 
 * Returns: address of the free block with lowest address 
 *  higher than the pointer of the block to free 
 */
heap_header* subsequent_free_block_in_list (heap_header* to_free_header_ptr) {
    
    heap_header* curr_ptr = free_blocks_tail_ptr;
    heap_header* next_ptr = NULL;
    
    while (curr_ptr != NULL && within_bounds(to_free_header_ptr, curr_ptr)) {
        next_ptr = curr_ptr;
        curr_ptr = get_prev_free_block_from_header (curr_ptr);
    }
    
    return next_ptr;
}


/**
 * Insert a node at the head of a linked list
 * 
 * Argument
 *  - free_header_ptr: pointer to the node to insert
 * 
 * Returns: n/a
 */
void insert_free_block_at_list_head (heap_header* free_header_ptr) {
    free_blocks_head_ptr = free_header_ptr;  
    free_blocks_tail_ptr = free_header_ptr; 
    heap_link free_link = link_factory (NULL, NULL);
    write_link (free_header_ptr, &free_link);
}


/**
 * Inserts free block in the body of a linked list
 * 
 * Argument
 *  - free_header_ptr: pointer to a free block
 * 
 * Returns: n/a
 */
void insert_free_block_in_list_body (heap_header* free_header_ptr) {

    heap_header* prev_header = precedent_free_block_in_list (free_header_ptr);
    heap_header* next_header = subsequent_free_block_in_list (free_header_ptr);
    heap_link home_free = link_factory (prev_header, next_header);
    write_link (free_header_ptr, &home_free);
    
    if (prev_header == NULL) {
        free_blocks_head_ptr = free_header_ptr;
        set_next_free_node (free_header_ptr, next_header);
    } else {
        set_next_free_node(prev_header, free_header_ptr);
    }

    if (next_header == NULL) {
        free_blocks_tail_ptr = free_header_ptr;        
        set_prev_free_node (free_header_ptr, prev_header);
    } else {
        set_prev_free_node(next_header, free_header_ptr);
    }
}


/**
 * Inserts free block linked list pointers
 * 
 * Argument
 *  - free_header_ptr: pointer to a free block
 * 
 * Returns: n/a
 */
void insert_free_block_in_linked_list (heap_header* free_header_ptr) {

    if (free_blocks_head_ptr == NULL) {
        insert_free_block_at_list_head (free_header_ptr);
    } else {
        insert_free_block_in_list_body (free_header_ptr);
    }
}


/**
 * Deletes free block linked list pointers
 * 
 * Argument
 *  - header_ptr: pointer to a block
 * 
 * Returns: n/a
 */
void delete_free_block_in_linked_list (heap_header* delete_ptr) {

    heap_header* prev_header = get_prev_free_block_from_header (delete_ptr);
    heap_header* next_header = get_next_free_block_from_header (delete_ptr);
    
    if (prev_header == NULL) {
       free_blocks_head_ptr = next_header; 
    } else {
       set_next_free_node (prev_header, next_header);
    }
    
    if (next_header == NULL) {
        free_blocks_tail_ptr = prev_header;
    } else {
        set_prev_free_node (next_header, prev_header);
    }
}


/**
 * Write the header for a free block
 * 
 * Argument:
 *  - header_ptr: pointer to the header
 *  - requested_size: requested payload size
 * 
 * Returns: n/a
 */
void write_free_block_header (heap_header* header_ptr, size_t block_bytes) {
    size_t padded_payload_bytes = block_bytes - block_overhead_bytes();
    heap_header header = header_factory (padded_payload_bytes, false);
    write_header (header_ptr, &header);
}


/**
 * Write the header for a used block
 * 
 * Argument:
 *  - header_ptr: pointer to the header
 *  - requested_size: requested payload size
 * 
 * Returns: n/a
 */
void write_used_block_header (heap_header* header_ptr, size_t block_bytes) {
    size_t padded_payload_bytes = block_bytes - block_overhead_bytes();
    heap_header header = header_factory (padded_payload_bytes, true);
    write_header (header_ptr, &header);
}


/**
 * Evaluates how many bytes can be coalesced to the right
 * 
 * Argument 
 *  - curr_block_ptr: pointer to a heap block
 *  - curr_paylod_bytes: size of the heap block
 * 
 * Returns: bytes free on the block to the right, zero of it is not free
 */ 
void coalescing_right_once (heap_header* curr_block_ptr,
                                    heap_header* next_block_ptr,
                                    size_t* eatable_block_bytes) {
    
    size_t block_size;
    
    if (within_bounds (next_block_ptr, heap_top(0)) &&
        !header_block_is_used (*next_block_ptr)) {

        block_size = block_overhead_bytes() + block_payload_size (next_block_ptr);
        *eatable_block_bytes = block_size;
    }

}


/**
 * Create a free block's explicit link,
 *  which encompasses current blocks, and another coalesced block
 * 
 * Argument
 *  - curr_header_ptr: pointer to current block header
 *  - curr_block_bytes: size of of the current block
 *  - eatable_bytes: size of the coalescced block(s) to the right
 * 
 * Returns: n/a
 */
void write_coalesced_free_super_block_link (heap_header* curr_header_ptr,
                                            heap_header* next_block_ptr) {
                                                
    insert_free_block_in_linked_list (curr_header_ptr);
    delete_free_block_in_linked_list (next_block_ptr);
}


/**
 * Free a block at a header, with a certain size
 * 
 * Argument
 *  - curr_header_ptr: pointer to the block header
 *  - curr_paylod_bytes: size of the block
 * 
 * Returns: n/a
 */
void free_heap_block (heap_header* curr_header_ptr, size_t curr_paylod_bytes) {
    
    size_t eatable_block_bytes = 0;
    
    size_t curr_block_bytes = block_overhead_bytes() + curr_paylod_bytes;
    
    heap_header* next_block_ptr = get_next_block_header (curr_header_ptr, curr_block_bytes);

    coalescing_right_once (curr_header_ptr, next_block_ptr, &eatable_block_bytes);
                                        
    size_t super_block_bytes = curr_block_bytes + eatable_block_bytes;

    if (eatable_block_bytes == 0) {
        // nothing to free
        write_free_block_header (curr_header_ptr, curr_block_bytes);
        insert_free_block_in_linked_list (curr_header_ptr);
        
    } else {
        // free block to the right
        write_free_block_header (curr_header_ptr, super_block_bytes);
        write_coalesced_free_super_block_link (curr_header_ptr, next_block_ptr);
    }
}


/**
 * Free memory previously allocated
 * 
 * Argument
 *  - ptr: pointer for the memory location
 */
void myfree (void *payload_ptr) {

    if (payload_ptr == NULL) {
        return;
    }
    
    heap_header* header_ptr = get_block_pointer_from_payload (payload_ptr);
    size_t payload_bytes = block_payload_size (header_ptr);
    free_heap_block (header_ptr, payload_bytes);
}


/**
 * Reuse a previously-freed block.  If there is enough space fragmented, 
 *  create a new split block, in additionn to the block to insert
 * 
 * Argument
 *  - insert_ptr: the location to place a block that was freed
 *  - padded_block_bytes: the amount of free memory available
 *  - padded_payload_bytes: the payload we want to store
 * 
 * Returns:
 *  - 0 if a single block will be inserted
 *  - >0 bytes allocted to the fragmented block
 * 
 */
void alloc_free_block (heap_header* insert_ptr, 
                       size_t padded_block_bytes, size_t padded_payload_bytes) {

    size_t free_size = block_payload_size (insert_ptr);
    
    // is there enough space to justify a split?
    if (free_size >= padded_block_bytes + min_block_size ()) {
        // partition: used 
        heap_header header_insert = header_factory (padded_payload_bytes, true);
        write_header (insert_ptr, &header_insert);
        // partition: free
        heap_header* split_ptr = get_next_block_header (insert_ptr, 
                                                        padded_block_bytes);
        size_t split_size = free_size - padded_block_bytes;
        free_heap_block (split_ptr, split_size);
        
    } else {
        // main
        heap_header header_insert = header_factory (free_size, true);
        write_header (insert_ptr, &header_insert);
    }
}


/**
 * Allocate a new memory block, not previously freed
 * 
 * Argument
 *  - insert_ptr: address of the block
 *  - padded_block_bytes: size of the block
 *  - padded_payload_bytes: size of the payload in the block
 */
void alloc_new_block (heap_header* insert_ptr, size_t padded_block_bytes, 
                     size_t padded_payload_bytes) {
    heap_header header_insert = header_factory (padded_payload_bytes, true);    
    write_header (insert_ptr, &header_insert);
    heap_link link_insert = link_factory (NULL, NULL);
    write_link (insert_ptr, &link_insert);
}


/**
 * Allocate memory in the heap
 * 
 * Arguments:
 *  requested_size: number of bytes requested
 */
void* mymalloc (size_t requested_size) {
    
    // exception
    if (!requested_size) {
        return NULL;
    }
    
    // scope
    size_t padded_block_bytes = valid_alloc (requested_size);
    size_t padded_payload_bytes = request_payload (padded_block_bytes);
    
    // identify location
    heap_header* insert_ptr = find_free_block (requested_size);
    bool is_reuse = true;
    if (insert_ptr == NULL) {
        insert_ptr = heap_top (0);
        is_reuse = false;
    }

    if (is_reuse) {
        alloc_free_block (insert_ptr, padded_block_bytes, padded_payload_bytes);
    } else {
        alloc_new_block (insert_ptr, padded_block_bytes, padded_payload_bytes);
    }
    
    // payload
    void *payload_ptr = get_block_payload_from_header (insert_ptr); 
    assert (payload_ptr != NULL);
    
    // update
    if (!is_reuse) {
        bytes_used += padded_block_bytes;
    }

    return payload_ptr;
}


/**
 * Attempt coalescing blocks to the right, to combine a certain size.
 *  Free contiguous-right blocks are coalesced, even if realloc in place ends 
 *  up failing because not enough continuous memory is availlable,
 *  to reduce external fragmentation
 * 
 * Argument
 *  - curr_header_ptr: pointer to the block header
 *  - curr_paylod_bytes: size of the block
 *  - target_bytes: the total bytes necessary for in-place realloc
 *  - total_eatable_bytes: the total bytes freed by coalescing so far
 * 
 * Returns: n/a
 */
heap_header* coalescing_right_target (heap_header* home_header_ptr, 
        size_t home_paylod_bytes, size_t target_bytes, size_t* super_block_bytes) {
    
    // home
    heap_header* curr_header_ptr = home_header_ptr;
    size_t home_block_bytes = block_overhead_bytes() + home_paylod_bytes;
    *super_block_bytes = home_block_bytes;
    
    size_t curr_block_bytes = home_block_bytes;
    heap_header* next_block_ptr;
    size_t curr_edible_bytes;
    heap_header* last_header_ptr;

    do {
        // current
        next_block_ptr = get_next_block_header (curr_header_ptr, curr_block_bytes);
        curr_edible_bytes = 0;
        coalescing_right_once (curr_header_ptr, next_block_ptr, &curr_edible_bytes);

        // update
        curr_block_bytes = curr_edible_bytes;
        last_header_ptr = curr_header_ptr;
        curr_header_ptr = next_block_ptr;
        *super_block_bytes += curr_edible_bytes;
        
        if (*super_block_bytes >= target_bytes) {
            return next_block_ptr;
        }

    } while (curr_edible_bytes != 0);

    return last_header_ptr;
}


/**
 * Performs the operatioins necessary to effect realloc in place
 * 
 * Argument
 *  - header_ptr: pointer to the super block
 *  - padded_block_bytes: padded bytes from the original client request
 *  - super_block_bytes: the free bytes found for coalescing
 * 
 * Returns: n/a
 */
void realloc_inplace (heap_header* home_ptr, heap_header* last_free_coalesced,
                      size_t padded_block_bytes, size_t super_block_bytes) {
 
    heap_header* first_free_coalesced = 
                get_next_block_header (home_ptr, padded_block_bytes);

    // delete free nodes for blocks to be reallocated
    heap_header* curr_ptr = first_free_coalesced;
    while (curr_ptr <= last_free_coalesced &&
           curr_ptr != NULL) {
        delete_free_block_in_linked_list (curr_ptr);
        curr_ptr = get_next_free_block_from_header (curr_ptr);
    }
    
    // apply splitting policy
    size_t padded_payload_bytes = request_payload (padded_block_bytes);
    alloc_free_block (home_ptr, super_block_bytes, padded_payload_bytes);
    
    // implicit
    write_used_block_header (home_ptr, super_block_bytes);
}


/**
 * Re-size previously-allocated memory block.
 * It allocates a new block, and moves existent content
 * 
 * Argument
 *  - old_payload_ptr: pointer to the pre-existing memory block
 *  - requested_size: desired size for the memory block
 */
void* myrealloc (void *old_payload_ptr, size_t requested_size) {

    // header
    heap_header* home_ptr = get_block_pointer_from_payload (old_payload_ptr);
    size_t old_payload_size = block_payload_size (home_ptr);

    // in-place, current block is big enough, either: 
    //  - size is shrinking, we can use the existing block
    //  - size is growing, but we had added padding to the existing block
    if (requested_size <= old_payload_size) {
        return old_payload_ptr;
    }

    // scope
    size_t padded_block_bytes = valid_alloc (requested_size);

    // in-place: 
    //  - size is growing, but adjacent blocks are free
    size_t super_block_bytes = 0;
    heap_header* last_free_coalesced = coalescing_right_target (home_ptr, 
            old_payload_size, padded_block_bytes,  &super_block_bytes);
    
    if (super_block_bytes >= padded_block_bytes) {
        
        size_t padded_old_size = valid_alloc(old_payload_size);

        realloc_inplace (home_ptr, last_free_coalesced,
                         padded_old_size, super_block_bytes);

        return old_payload_ptr;
    }

    // just malloc:
    // allocate
    void* new_ptr = mymalloc (requested_size);
    assert (new_ptr != NULL);
    // copy
    size_t size = requested_size > old_payload_size? old_payload_size : requested_size; 
    memcpy (new_ptr, old_payload_ptr, size);
    // free
    myfree (old_payload_ptr);
    
    return new_ptr;    
}


/**
 * Dump the raw heap contents, printing block headers.
 * You can then call the function from gdb to view the contents of the heap 
 * segment. 
 * 
 * Arguments: n/a
 * 
 * Returns: n/a
 */
void dump_heap_headers () {

    printf ("\n==== HEADER DUMP\n");

    // heap
    void* ptr = segment_start; 
    void* heap_end = heap_top (0);
    
    // header
    heap_header header;

    while (within_bounds (ptr, heap_end)) {
        
        // current
        read_header (&header, ptr);
        void* payload_ptr = (char*) ptr + block_overhead_bytes();
        printf ("- client_ptr=%p is_used=%u size=%lu \n", 
            payload_ptr, header_block_is_used (header), header_payload_size (header));
        
        // next
        ptr = get_next_implicit_header (header, ptr);
    }
}


/**
 * Validates the explicit linked list as part of heap validation
 * 
 * Argument: n/a
 * 
 * Returns: true/false on heap validaity
 */
bool valid_explicit_heap () {
    
    heap_header* curr_header;
    size_t head_to_tail_block_count = 0;
    size_t tail_to_head_block_count = 0;
        
    // head to tail
    curr_header = free_blocks_head_ptr;
    while (curr_header != NULL){
        head_to_tail_block_count += 1;
        curr_header = get_next_free_block_from_header (curr_header);
    }
    
    // tail to head
    curr_header = free_blocks_tail_ptr;
    while (curr_header != NULL){
        tail_to_head_block_count += 1;
        curr_header = get_prev_free_block_from_header (curr_header); 
    }
    
    if (head_to_tail_block_count != tail_to_head_block_count) {
        return false;
    }
    
    return true;
}


/**
 * Validates the implicit headers of heap validation
 * 
 * Argument: n/a
 * 
 * Returns: true/false on heap validaity
 */
bool valid_implicit_heap () {
    
    // heap
    void* ptr = segment_start; 
    void* heap_end = heap_top (0);
    
    // header
    heap_header header;

    while (within_bounds (ptr, heap_end)) {
        // current
        read_header (&header, ptr);
        // next
        ptr = get_next_implicit_header (header, ptr);
    }
    
    // error?
    if (ptr != heap_end) {
        dump_heap_headers();
        return false;
    }
 
    return true;
}


/**
 * Asserts the validity of the heap state
 * 
 * Call to breakpoint() stops gdb to poke around
 */
bool validate_heap () {
    
    if (segment_start == NULL) {
        printf ("\n Oops! Null segment_start!\n");
        breakpoint();   
        return false;
    }
    
    if (segment_size == 0) {
        printf ("\n Oops! Zero segment_size!\n");
        breakpoint();   
        return false;
    }
    
    if (bytes_used > segment_size) {
        printf ("\n Oops! Have used more heap than total available?!\n");
        breakpoint();   
        return false;
    }
    
    if (!valid_implicit_heap ()) {
        printf ("\n Oops! Invalid implicit heap!\n");
        breakpoint();   
        return false;        
    }
    
    if (!valid_explicit_heap ()) {
        printf ("\n Oops! Invalid explicit heap!\n");
        breakpoint();   
        return false;        
    }
    
    return true;
}


/**
 * Dump the raw heap contents, printing block headers.
 * You can then call the function from gdb to view the contents of the heap 
 * segment. 
 * 
 * Arguments: n/a
 * 
 * Returns: n/a
 */
void dump_heap_bytes () {
    
    size_t LINE_COUNT = 32; // 8
    
    printf ("\n==== HEAP SUMMARY");
    printf ("\nstarts=%p", segment_start);
    printf ("\nends=%p", (char*) segment_start + segment_size); 
    
    printf ("\n\n==== BYTE DUMP");
    printf ("\nbytes=%lu\n", bytes_used);
        
    for (int i = 0; i < bytes_used; i++) {
        unsigned char *cur = (unsigned char*) segment_start + i;
        if (i % LINE_COUNT == 0) {
            printf ("\n%p: ", cur);
        }
        printf ("%02x ", *cur);
    }
    
    printf ("\n\n");
}


