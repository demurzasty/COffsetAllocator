# COffsetAllocator

> This is a port of [Sebastian Aaltonen's OffsetAllocator](https://github.com/sebbbi/OffsetAllocator) for C.
> It's a fast, simple, hard real time allocator.  
> This is especially useful for managing GPU resources,
> and the goal is to use it in [WILD](https://github.com/demurzasty/WILD).

# Description

From the original README:

> Fast hard realtime O(1) offset allocator with minimal fragmentation.
>
> Uses 256 bins with 8 bit floating point distribution (3 bit mantissa + 5 bit exponent) and a two level bitfield to find the next available bin using 2x LZCNT instructions to make all operations O(1). Bin sizes following the floating point distribution ensures hard bounds for memory overhead percentage regarless of size class. Pow2 bins would waste up to +100% memory (+50% on average). Our float bins waste up to +12.5% (+6.25% on average).
>
> The allocation metadata is stored in a separate data structure, making this allocator suitable for sub-allocating any resources, such as GPU heaps, buffers and arrays. Returns an offset to the first element of the allocated contiguous range.

# How to use

```c
#include <OA/COffsetAllocator.h>

#include <stdio.h>

int main(int argc, char* argv[]) {
    OA_OffsetAllocator allocator;
    OA_Init(&allocator, 12345, 128 * 1024);

    OA_Allocation a = OA_Allocate(&allocator, 1337);
    printf("offsetA: %u\n", a.offset);

    OA_Allocation b = OA_Allocate(&allocator, 123);
    printf("offsetB: %u\n", b.offset);

    OA_Free(&allocator, a);

    OA_Allocation c = OA_Allocate(&allocator, 64);
    printf("offsetB: %u\n", c.offset);

    OA_Free(&allocator, b);
    OA_Free(&allocator, c);

    OA_Destroy(&allocator);
    return 0;
}
```

# Author

Author
C++ version: Sebastian Aaltonen

C port: Mariusz Dzikowski, @demurzasty

# License

Licensed under the MIT license. See `LICENSE` for details.
