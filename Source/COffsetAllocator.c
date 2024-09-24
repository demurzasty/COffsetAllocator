#include <OA/COffsetAllocator.h>

// This is a port of Sebastian Aaltonen's OffsetAllocator for C.
// See: https://github.com/sebbbi/OffsetAllocator

#ifndef NDEBUG
#    include <assert.h>
#    define OA_ASSERT(x) assert(x)
#else
#    define OA_ASSERT(x) ((void)0)
#endif

#ifdef _MSC_VER
#    include <intrin.h>
#endif

#define OA_NO_SPACE 0xffffffff
#define OA_NODE_UNUSED 0xffffffff

#define OA_MANTISSA_BITS 3
#define OA_MANTISSA_VALUE (1 << OA_MANTISSA_BITS)
#define OA_MANTISSA_MASK (OA_MANTISSA_VALUE - 1)

struct OA_Node {
    uint32_t dataOffset;
    uint32_t dataSize;
    OA_NodeIndex binListPrev;
    OA_NodeIndex binListNext;
    OA_NodeIndex neighborPrev;
    OA_NodeIndex neighborNext;
    uint8_t used;
};

#ifdef _MSC_VER
static uint32_t OA_CountLeadingZeros(uint32_t v) {
    unsigned long retVal;
    _BitScanReverse(&retVal, v);
    return 31 - retVal;
}
#else
#    define OA_CountLeadingZeros(v) __builtin_clz(v)
#endif

#ifdef _MSC_VER
static uint32_t OA_CountTrailingZeros(uint32_t v) {
    unsigned long retVal;
    _BitScanForward(&retVal, v);
    return retVal;
}
#else
#    define OA_CountTrailingZeros(v) __builtin_ctz(v)
#endif

// Bin sizes follow floating point (exponent + mantissa) distribution (piecewise linear log approx)
// This ensures that for each size class, the average overhead percentage stays the same
static uint32_t OA_UIntToFloatRoundUp(uint32_t size) {
    uint32_t exp = 0;
    uint32_t mantissa = 0;

    if (size < OA_MANTISSA_VALUE) {
        // Denorm: 0..(MANTISSA_VALUE-1)
        mantissa = size;
    } else {
        // Normalized: Hidden high bit always 1. Not stored. Just like float.
        uint32_t leadingZeros = OA_CountLeadingZeros(size);
        uint32_t highestSetBit = 31 - leadingZeros;

        uint32_t mantissaStartBit = highestSetBit - OA_MANTISSA_BITS;
        exp = mantissaStartBit + 1;
        mantissa = (size >> mantissaStartBit) & OA_MANTISSA_MASK;

        uint32_t lowBitsMask = (1 << mantissaStartBit) - 1;

        // Round up!
        if ((size & lowBitsMask) != 0) {
            mantissa++;
        }
    }

    return (exp << OA_MANTISSA_BITS) + mantissa; // + allows mantissa->exp overflow for round up
}

static uint32_t OA_UIntToFloatRoundDown(uint32_t size) {
    uint32_t exp = 0;
    uint32_t mantissa = 0;

    if (size < OA_MANTISSA_VALUE) {
        // Denorm: 0..(MANTISSA_VALUE-1)
        mantissa = size;
    } else {
        // Normalized: Hidden high bit always 1. Not stored. Just like float.
        uint32_t leadingZeros = OA_CountLeadingZeros(size);
        uint32_t highestSetBit = 31 - leadingZeros;

        uint32_t mantissaStartBit = highestSetBit - OA_MANTISSA_BITS;
        exp = mantissaStartBit + 1;
        mantissa = (size >> mantissaStartBit) & OA_MANTISSA_MASK;
    }

    return (exp << OA_MANTISSA_BITS) | mantissa;
}

static uint32_t OA_FloatToUInt(uint32_t floatValue) {
    uint32_t exponent = floatValue >> OA_MANTISSA_BITS;
    uint32_t mantissa = floatValue & OA_MANTISSA_MASK;
    if (exponent == 0) {
        // Denorms
        return mantissa;
    } else {
        return (mantissa | OA_MANTISSA_VALUE) << (exponent - 1);
    }
}

static uint32_t OA_FindLowestSetBitAfter(uint32_t bitMask, uint32_t startBitIndex) {
    uint32_t maskBeforeStartIndex = (1 << startBitIndex) - 1;
    uint32_t maskAfterStartIndex = ~maskBeforeStartIndex;
    uint32_t bitsAfter = bitMask & maskAfterStartIndex;
    if (bitsAfter == 0) {
        return OA_NO_SPACE;
    }
    return OA_CountTrailingZeros(bitsAfter);
}

static void OA_InitNode(OA_Node* node) {
    node->dataOffset = 0;
    node->dataSize = 0;
    node->binListPrev = OA_NODE_UNUSED;
    node->binListNext = OA_NODE_UNUSED;
    node->neighborPrev = OA_NODE_UNUSED;
    node->neighborNext = OA_NODE_UNUSED;
    node->used = 0;
}

static uint32_t OA_InsertNodeIntoBin(OA_OffsetAllocator* offsetAllocator, uint32_t size, uint32_t dataOffset) {
    // Round down to bin index to ensure that bin >= alloc
    uint32_t binIndex = OA_UIntToFloatRoundDown(size);

    uint32_t topBinIndex = binIndex >> OA_TOP_BINS_INDEX_SHIFT;
    uint32_t leafBinIndex = binIndex & OA_LEAF_BINS_INDEX_MASK;

    // Bin was empty before?
    if (offsetAllocator->binIndices[binIndex] == OA_NODE_UNUSED) {
        // Set bin mask bits
        offsetAllocator->usedBins[topBinIndex] |= 1 << leafBinIndex;
        offsetAllocator->usedBinsTop |= 1 << topBinIndex;
    }

    // Take a freelist node and insert on top of the bin linked list (next = old top)
    uint32_t topNodeIndex = offsetAllocator->binIndices[binIndex];
    uint32_t nodeIndex = offsetAllocator->freeNodes[offsetAllocator->freeOffset--];

    OA_InitNode(&offsetAllocator->nodes[nodeIndex]);
    offsetAllocator->nodes[nodeIndex].dataOffset = dataOffset;
    offsetAllocator->nodes[nodeIndex].dataSize = size;
    offsetAllocator->nodes[nodeIndex].binListNext = topNodeIndex;

    if (topNodeIndex != OA_NODE_UNUSED) {
        offsetAllocator->nodes[topNodeIndex].binListPrev = nodeIndex;
    }

    offsetAllocator->binIndices[binIndex] = nodeIndex;

    offsetAllocator->freeStorage += size;

    return nodeIndex;
}

static void OA_RemoveNodeFromBin(OA_OffsetAllocator* offsetAllocator, uint32_t nodeIndex) {
    OA_Node* node = &offsetAllocator->nodes[nodeIndex];

    if (node->binListPrev != OA_NODE_UNUSED) {
        // Easy case: We have previous node-> Just remove this node from the middle of the list.
        offsetAllocator->nodes[node->binListPrev].binListNext = node->binListNext;
        if (node->binListNext != OA_NODE_UNUSED) offsetAllocator->nodes[node->binListNext].binListPrev = node->binListPrev;
    } else {
        // Hard case: We are the first node in a bin. Find the bin.

        // Round down to bin index to ensure that bin >= alloc
        uint32_t binIndex = OA_UIntToFloatRoundDown(node->dataSize);

        uint32_t topBinIndex = binIndex >> OA_TOP_BINS_INDEX_SHIFT;
        uint32_t leafBinIndex = binIndex & OA_LEAF_BINS_INDEX_MASK;

        offsetAllocator->binIndices[binIndex] = node->binListNext;
        if (node->binListNext != OA_NODE_UNUSED) offsetAllocator->nodes[node->binListNext].binListPrev = OA_NODE_UNUSED;

        // Bin empty?
        if (offsetAllocator->binIndices[binIndex] == OA_NODE_UNUSED) {
            // Remove a leaf bin mask bit
            offsetAllocator->usedBins[topBinIndex] &= ~(1 << leafBinIndex);

            // All leaf bins empty?
            if (offsetAllocator->usedBins[topBinIndex] == 0) {
                // Remove a top bin mask bit
                offsetAllocator->usedBinsTop &= ~(1 << topBinIndex);
            }
        }
    }

    offsetAllocator->freeNodes[++offsetAllocator->freeOffset] = nodeIndex;
    offsetAllocator->freeStorage -= node->dataSize;
}

void OA_Init(OA_OffsetAllocator* offsetAllocator, uint32_t size, uint32_t maxAllocs) {
    offsetAllocator->size = size;
    offsetAllocator->maxAllocs = maxAllocs;
    offsetAllocator->nodes = NULL;
    offsetAllocator->freeNodes = NULL;

    OA_Reset(offsetAllocator);
}

void OA_Destroy(OA_OffsetAllocator* offsetAllocator) {
    free(offsetAllocator->nodes);
    free(offsetAllocator->freeNodes);
}

void OA_Reset(OA_OffsetAllocator* offsetAllocator) {
    offsetAllocator->freeStorage = 0;
    offsetAllocator->usedBinsTop = 0;
    offsetAllocator->freeOffset = offsetAllocator->maxAllocs - 1;

    for (uint32_t i = 0; i < OA_NUM_TOP_BINS; i++) {
        offsetAllocator->usedBins[i] = 0;
    }

    for (uint32_t i = 0; i < OA_NUM_LEAF_BINS; i++) {
        offsetAllocator->binIndices[i] = OA_NODE_UNUSED;
    }

    free(offsetAllocator->nodes);
    free(offsetAllocator->freeNodes);

    offsetAllocator->nodes = (OA_Node*)malloc(offsetAllocator->maxAllocs * sizeof(OA_Node));
    offsetAllocator->freeNodes = (OA_NodeIndex*)malloc(offsetAllocator->maxAllocs * sizeof(OA_NodeIndex));

    for (uint32_t i = 0; i < offsetAllocator->maxAllocs; ++i) {
        OA_InitNode(&offsetAllocator->nodes[i]);
    }

    // Freelist is a stack. Nodes in inverse order so that [0] pops first.
    for (uint32_t i = 0; i < offsetAllocator->maxAllocs; i++) {
        offsetAllocator->freeNodes[i] = offsetAllocator->maxAllocs - i - 1;
    }

    // Start state: Whole storage as one big node
    // Algorithm will split remainders and push them back as smaller nodes
    OA_InsertNodeIntoBin(offsetAllocator, offsetAllocator->size, 0);
}

OA_Allocation OA_Allocate(OA_OffsetAllocator* offsetAllocator, uint32_t size) {
    // Out of allocations?
    if (offsetAllocator->freeOffset == 0) {
        OA_Allocation allocation;
        allocation.offset = OA_NO_SPACE;
        allocation.metadata = OA_NO_SPACE;
        return allocation;
    }

    // Round up to bin index to ensure that alloc >= bin
    // Gives us min bin index that fits the size
    uint32_t minBinIndex = OA_UIntToFloatRoundUp(size);

    uint32_t minTopBinIndex = minBinIndex >> OA_TOP_BINS_INDEX_SHIFT;
    uint32_t minLeafBinIndex = minBinIndex & OA_LEAF_BINS_INDEX_MASK;

    uint32_t topBinIndex = minTopBinIndex;
    uint32_t leafBinIndex = OA_NO_SPACE;

    // If top bin exists, scan its leaf bin. This can fail (NO_SPACE).
    if (offsetAllocator->usedBinsTop & (1 << topBinIndex)) {
        leafBinIndex = OA_FindLowestSetBitAfter(offsetAllocator->usedBins[topBinIndex], minLeafBinIndex);
    }

    // If we didn't find space in top bin, we search top bin from +1
    if (leafBinIndex == OA_NO_SPACE) {
        topBinIndex = OA_FindLowestSetBitAfter(offsetAllocator->usedBinsTop, minTopBinIndex + 1);

        // Out of space?
        if (topBinIndex == OA_NO_SPACE) {
            OA_Allocation allocation;
            allocation.offset = OA_NO_SPACE;
            allocation.metadata = OA_NO_SPACE;
            return allocation;
        }

        // All leaf bins here fit the alloc, since the top bin was rounded up. Start leaf search from bit 0.
        // NOTE: This search can't fail since at least one leaf bit was set because the top bit was set.
        leafBinIndex = OA_CountTrailingZeros(offsetAllocator->usedBins[topBinIndex]);
    }

    uint32_t binIndex = (topBinIndex << OA_TOP_BINS_INDEX_SHIFT) | leafBinIndex;

    // Pop the top node of the bin. Bin top = node->next.
    uint32_t nodeIndex = offsetAllocator->binIndices[binIndex];
    OA_Node* node = &offsetAllocator->nodes[nodeIndex];
    uint32_t nodeTotalSize = node->dataSize;
    node->dataSize = size;
    node->used = 1;
    offsetAllocator->binIndices[binIndex] = node->binListNext;
    if (node->binListNext != OA_NODE_UNUSED) {
        offsetAllocator->nodes[node->binListNext].binListPrev = OA_NODE_UNUSED;
    }

    offsetAllocator->freeStorage -= nodeTotalSize;

    // Bin empty?
    if (offsetAllocator->binIndices[binIndex] == OA_NODE_UNUSED) {
        // Remove a leaf bin mask bit
        offsetAllocator->usedBins[topBinIndex] &= ~(1 << leafBinIndex);

        // All leaf bins empty?
        if (offsetAllocator->usedBins[topBinIndex] == 0) {
            // Remove a top bin mask bit
            offsetAllocator->usedBinsTop &= ~(1 << topBinIndex);
        }
    }

    // Push back reminder N elements to a lower bin
    uint32_t reminderSize = nodeTotalSize - size;
    if (reminderSize > 0) {
        uint32_t newNodeIndex = OA_InsertNodeIntoBin(offsetAllocator, reminderSize, node->dataOffset + size);

        // Link nodes next to each other so that we can merge them later if both are free
        // And update the old next neighbor to point to the new node (in middle)
        if (node->neighborNext != OA_NODE_UNUSED) offsetAllocator->nodes[node->neighborNext].neighborPrev = newNodeIndex;
        offsetAllocator->nodes[newNodeIndex].neighborPrev = nodeIndex;
        offsetAllocator->nodes[newNodeIndex].neighborNext = node->neighborNext;
        node->neighborNext = newNodeIndex;
    }

    OA_Allocation allocation;
    allocation.offset = node->dataOffset;
    allocation.metadata = nodeIndex;
    return allocation;
}

void OA_Free(OA_OffsetAllocator* offsetAllocator, OA_Allocation allocation) {
    OA_ASSERT(allocation.metadata != OA_NO_SPACE);
    if (!offsetAllocator->nodes) {
        return;
    }

    uint32_t nodeIndex = allocation.metadata;
    OA_Node* node = &offsetAllocator->nodes[nodeIndex];

    // Double delete check
    OA_ASSERT(node->used == 1);

    // Merge with neighbors...
    uint32_t offset = node->dataOffset;
    uint32_t size = node->dataSize;

    if ((node->neighborPrev != OA_NODE_UNUSED) && (offsetAllocator->nodes[node->neighborPrev].used == 0)) {
        // Previous (contiguous) free node: Change offset to previous node offset. Sum sizes
        OA_Node* prevNode = &offsetAllocator->nodes[node->neighborPrev];
        offset = prevNode->dataOffset;
        size += prevNode->dataSize;

        // Remove node from the bin linked list and put it in the freelist
        OA_RemoveNodeFromBin(offsetAllocator, node->neighborPrev);

        OA_ASSERT(prevNode->neighborNext == nodeIndex);
        node->neighborPrev = prevNode->neighborPrev;
    }

    if ((node->neighborNext != OA_NODE_UNUSED) && (offsetAllocator->nodes[node->neighborNext].used == 0)) {
        // Next (contiguous) free node: Offset remains the same. Sum sizes.
        OA_Node* nextNode = &offsetAllocator->nodes[node->neighborNext];
        size += nextNode->dataSize;

        // Remove node from the bin linked list and put it in the freelist
        OA_RemoveNodeFromBin(offsetAllocator, node->neighborNext);

        OA_ASSERT(nextNode->neighborPrev == nodeIndex);
        node->neighborNext = nextNode->neighborNext;
    }

    uint32_t neighborNext = node->neighborNext;
    uint32_t neighborPrev = node->neighborPrev;

    // Insert the removed node to freelist
    offsetAllocator->freeNodes[++offsetAllocator->freeOffset] = nodeIndex;

    // Insert the (combined) free node to bin
    uint32_t combinedNodeIndex = OA_InsertNodeIntoBin(offsetAllocator, size, offset);

    // Connect neighbors with the new combined node
    if (neighborNext != OA_NODE_UNUSED) {
        offsetAllocator->nodes[combinedNodeIndex].neighborNext = neighborNext;
        offsetAllocator->nodes[neighborNext].neighborPrev = combinedNodeIndex;
    }
    if (neighborPrev != OA_NODE_UNUSED) {
        offsetAllocator->nodes[combinedNodeIndex].neighborPrev = neighborPrev;
        offsetAllocator->nodes[neighborPrev].neighborNext = combinedNodeIndex;
    }
}
