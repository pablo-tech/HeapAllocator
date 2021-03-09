# Dynamic Heap Memory Allocator
C heap allocator, enhancements over Bryant and O'Hallaron's Computer Systems


## Implicit Memory Allocator

Some key design choices and outcomes--
- A key strength of Implicit over Explicit is its simplicity. And performance
was significant, achieving average utilization performance of 74.71% over the
complete set of test scripts: example*, pattern*, trace*.  Inflexibility is its
major weakness, making it harder to add features like coalescing or in-place reallocc
- Optimized by applying Best Fit allocation policy to minimize space, even if 
at the expense of time. Then, optimized time, by applying the Ofast flag at 
compilation, which reduced the number of instructions per request by an order 
of magnitude
- In heap validation added a valid_implicit_heap() method that checks wether the 
heap "adds up". For instance, iterating over all headers should lead to 
the heap top
- Key edge cases include realloc, which can be to a larger block or to a 
smaller block.  Since memory is copied, how much to copy depends on the case
- Using the same logic for myfree simplified the code, resulting on a bug fix 
that major utilization boost for Implicit


## Explicit Memory Allocator
- With coalescing alone, utilization performance increased to 80.42%, and
79.00 % with coalescing and realloc-in-place. This is over the complete set of 
test scripts: example*, pattern*, trace*.  A key strength of Explicit is the 
ease with which new features can be added, thanks to the double-linked list, 
which reduces search for free nodes in large heaps. As a weakness, Explicit heap 
has a larger minimum size block, sufficient to store the linked list's pointers, 
likely resulting in greater internal fragmentation. To achieve high code quality, 
much better factoring and reuse is necessary
- The list of free blocks is maintained in address order, where the address of 
a current block in the list is less than the address of the next block. This 
results in a O(n) linear search for list insertion at delete time, looking for
the current block's previous/next blocks. It is estimated that this approach 
enjoys better memory utilization than LIFO-ordered Best-Fit allocation policy,
by leaving larger empty memory blocks deeper in the heap
- In heap validation added a valid_explicit_heap() method that checks wether the heap
"adds up". It counts the number of free nodes from head to tail, and viceversa,
assuring the counts match
- The find_free_block method emerged as a critical performance bottleneck, 
particularly with this test pattern: trace-firefox.script. Initially 
it resulted in the execution of 19,392,969,082 instructions over 26949 function 
calls.  Once the free node linked list was implemented, this reduced to 
6,553,425,133 instrucctions over the same number of function calls 


Insigh that arose --
- Studying hotspots was useful to focus attention on code that, as it turned out,
had small performance bugs that majorly impacted utilization.  The perfomance
increase after fixing them was a delight.  Cases in point for trace-firefox:
    * the application of the Best Fit policy inside find_free_block
        261,995,886      if (size >= padded_payload_bytes)
    * iterating over the linked list to insert newly freed blocks:
        256,317,413      while (curr_ptr != NULL && 
                                within_bounds(curr_ptr, to_free_header_ptr))
- Writing Implicit for portability to Explicit really paid off.  And in fact,
as I ported and extended the code for Explicit, I kept going back to update 
Implicit.  Surely inheritance would have been a better approach.  That aside,
it was possible to test a lot of code in the enhanced implicit, with better
naming and error handling, much before it was possible to validate those changes
in Explicit
- Found GDB examine more useful than implementing the heap_dump code.  This is 
especially true early in the development well before the dump code matured:
    define dump_heap
        call dump_heap_headers()
        # call dump_heap_bytes()
        x/10gx (char*) segment_start
    end
    
    b validate_heap
    command
    dump_heap
    end
- Logging was a key to success, to be able to see the whole sequence of method
calls and the response by key methods in the implementation.  It was helpful 
to always be referring to the pointer to the payload, not the header, 
as that is the address that re-alloc/free requests reference
    printf("- client_ptr=%p is_used=%u size=%lu \n", 

