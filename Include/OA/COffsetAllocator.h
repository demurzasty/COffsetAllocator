#ifndef _COFFSET_ALLOCATOR_H_
#define _COFFSET_ALLOCATOR_H_

// This is a port of Sebastian Aaltonen's OffsetAllocator for C.
// See: https://github.com/sebbbi/OffsetAllocator

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define OA_NUM_TOP_BINS 32
#define OA_BINS_PER_LEAF 8
#define OA_TOP_BINS_INDEX_SHIFT 3
#define OA_LEAF_BINS_INDEX_MASK 0x7
#define OA_NUM_LEAF_BINS (OA_NUM_TOP_BINS * OA_BINS_PER_LEAF)

typedef uint32_t OA_NodeIndex;

typedef struct OA_Allocation {
    uint32_t offset;
    OA_NodeIndex metadata;
} OA_Allocation;

typedef struct OA_Node OA_Node;

typedef struct OA_OffsetAllocator {
    uint32_t size;
    uint32_t maxAllocs;
    uint32_t freeStorage;

    uint32_t usedBinsTop;
    uint8_t usedBins[OA_NUM_TOP_BINS];
    uint32_t binIndices[OA_NUM_LEAF_BINS];

    OA_Node* nodes;
    OA_NodeIndex* freeNodes;
    uint32_t freeOffset;
} OA_OffsetAllocator;

extern void OA_Init(OA_OffsetAllocator* offsetAllocator, uint32_t size, uint32_t maxAllocs);

extern void OA_Destroy(OA_OffsetAllocator* offsetAllocator);

extern void OA_Reset(OA_OffsetAllocator* offsetAllocator);

extern OA_Allocation OA_Allocate(OA_OffsetAllocator* offsetAllocator, uint32_t size);

extern void OA_Free(OA_OffsetAllocator* offsetAllocator, OA_Allocation allocation);

#endif
