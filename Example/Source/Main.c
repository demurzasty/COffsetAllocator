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
