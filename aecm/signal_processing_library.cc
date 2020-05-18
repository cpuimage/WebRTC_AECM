/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "signal_processing_library.h"

// TODO(bugs.webrtc.org/9553): These function pointers are useless. Refactor
// things so that we simply have a bunch of regular functions with different
// implementations for different platforms.

#if defined(WEBRTC_HAS_NEON)

const MaxAbsValueW16 WebRtcSpl_MaxAbsValueW16 = WebRtcSpl_MaxAbsValueW16Neon;
const MaxAbsValueW32 WebRtcSpl_MaxAbsValueW32 = WebRtcSpl_MaxAbsValueW32Neon;
const MaxValueW16 WebRtcSpl_MaxValueW16 = WebRtcSpl_MaxValueW16Neon;
const MaxValueW32 WebRtcSpl_MaxValueW32 = WebRtcSpl_MaxValueW32Neon;
const MinValueW16 WebRtcSpl_MinValueW16 = WebRtcSpl_MinValueW16Neon;
const MinValueW32 WebRtcSpl_MinValueW32 = WebRtcSpl_MinValueW32Neon;


#elif defined(MIPS32_LE)

const MaxAbsValueW16 WebRtcSpl_MaxAbsValueW16 = WebRtcSpl_MaxAbsValueW16_mips;
const MaxAbsValueW32 WebRtcSpl_MaxAbsValueW32 =
#ifdef MIPS_DSP_R1_LE
    WebRtcSpl_MaxAbsValueW32_mips;
#else
    WebRtcSpl_MaxAbsValueW32C;
#endif
const MaxValueW16 WebRtcSpl_MaxValueW16 = WebRtcSpl_MaxValueW16_mips;
const MaxValueW32 WebRtcSpl_MaxValueW32 = WebRtcSpl_MaxValueW32_mips;
const MinValueW16 WebRtcSpl_MinValueW16 = WebRtcSpl_MinValueW16_mips;
const MinValueW32 WebRtcSpl_MinValueW32 = WebRtcSpl_MinValueW32_mips;


#else

const MaxAbsValueW16 WebRtcSpl_MaxAbsValueW16 = WebRtcSpl_MaxAbsValueW16C;
const MaxAbsValueW32 WebRtcSpl_MaxAbsValueW32 = WebRtcSpl_MaxAbsValueW32C;
const MaxValueW16 WebRtcSpl_MaxValueW16 = WebRtcSpl_MaxValueW16C;
const MaxValueW32 WebRtcSpl_MaxValueW32 = WebRtcSpl_MaxValueW32C;
const MinValueW16 WebRtcSpl_MinValueW16 = WebRtcSpl_MinValueW16C;
const MinValueW32 WebRtcSpl_MinValueW32 = WebRtcSpl_MinValueW32C;

#endif

// Table used by WebRtcSpl_CountLeadingZeros32_NotBuiltin. For each uint32_t n
// that's a sequence of 0 bits followed by a sequence of 1 bits, the entry at
// index (n * 0x8c0b2891) >> 26 in this table gives the number of zero bits in
// n.
const int8_t kWebRtcSpl_CountLeadingZeros32_Table[64] = {
        32, 8, 17, -1, -1, 14, -1, -1, -1, 20, -1, -1, -1, 28, -1, 18,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 26, 25, 24,
        4, 11, 23, 31, 3, 7, 10, 16, 22, 30, -1, -1, 2, 6, 13, 9,
        -1, 15, -1, 21, -1, 29, 19, -1, -1, -1, -1, -1, 1, 27, 5, 12,
};
/*
 * Algorithm:
 * Successive approximation of the equation (root + delta) ^ 2 = N
 * until delta < 1. If delta < 1 we have the integer part of SQRT (N).
 * Use delta = 2^i for i = 15 .. 0.
 *
 * Output precision is 16 bits. Note for large input values (close to
 * 0x7FFFFFFF), bit 15 (the highest bit of the low 16-bit half word)
 * contains the MSB information (a non-sign value). Do with caution
 * if you need to cast the output to int16_t type.
 *
 * If the input value is negative, it returns 0.
 */

#define WEBRTC_SPL_SQRT_ITER(N)                 \
  try1 = root + (1 << (N));                     \
  if (value >= try1 << (N))                     \
  {                                             \
    value -= try1 << (N);                       \
    root |= 2 << (N);                           \
  }

int32_t WebRtcSpl_SqrtFloor(int32_t value) {
    int32_t root = 0, try1;

    WEBRTC_SPL_SQRT_ITER (15);
    WEBRTC_SPL_SQRT_ITER (14);
    WEBRTC_SPL_SQRT_ITER (13);
    WEBRTC_SPL_SQRT_ITER (12);
    WEBRTC_SPL_SQRT_ITER (11);
    WEBRTC_SPL_SQRT_ITER (10);
    WEBRTC_SPL_SQRT_ITER (9);
    WEBRTC_SPL_SQRT_ITER (8);
    WEBRTC_SPL_SQRT_ITER (7);
    WEBRTC_SPL_SQRT_ITER (6);
    WEBRTC_SPL_SQRT_ITER (5);
    WEBRTC_SPL_SQRT_ITER (4);
    WEBRTC_SPL_SQRT_ITER (3);
    WEBRTC_SPL_SQRT_ITER (2);
    WEBRTC_SPL_SQRT_ITER (1);
    WEBRTC_SPL_SQRT_ITER (0);

    return root >> 1;
}

uint32_t WebRtcSpl_DivU32U16(uint32_t num, uint16_t den) {
    // Guard against division with 0
    if (den != 0) {
        return (uint32_t) (num / den);
    } else {
        return (uint32_t) 0xFFFFFFFF;
    }
}

int32_t WebRtcSpl_DivW32W16(int32_t num, int16_t den) {
    // Guard against division with 0
    if (den != 0) {
        return (int32_t) (num / den);
    } else {
        return (int32_t) 0x7FFFFFFF;
    }
}


static const uint32_t kMaxSeedUsed = 0x80000000;


static uint32_t IncreaseSeed(uint32_t *seed) {
    seed[0] = (seed[0] * ((int32_t) 69069) + 1) & (kMaxSeedUsed - 1);
    return seed[0];
}

int16_t WebRtcSpl_RandU(uint32_t *seed) {
    return (int16_t) (IncreaseSeed(seed) >> 16);
}

// Creates an array of uniformly distributed variables.
int16_t WebRtcSpl_RandUArray(int16_t *vector,
                             int16_t vector_length,
                             uint32_t *seed) {
    int i;
    for (i = 0; i < vector_length; i++) {
        vector[i] = WebRtcSpl_RandU(seed);
    }
    return vector_length;
}

// TODO(bjorn/kma): Consolidate function pairs (e.g. combine
//   WebRtcSpl_MaxAbsValueW16C and WebRtcSpl_MaxAbsIndexW16 into a single one.)
// TODO(kma): Move the next six functions into min_max_operations_c.c.

// Maximum absolute value of word16 vector. C version for generic platforms.
int16_t WebRtcSpl_MaxAbsValueW16C(const int16_t *vector, size_t length) {
    size_t i = 0;
    int absolute = 0, maximum = 0;

    RTC_DCHECK_GT(length, 0);

    for (i = 0; i < length; i++) {
        absolute = abs((int) vector[i]);

        if (absolute > maximum) {
            maximum = absolute;
        }
    }

    // Guard the case for abs(-32768).
    if (maximum > WEBRTC_SPL_WORD16_MAX) {
        maximum = WEBRTC_SPL_WORD16_MAX;
    }

    return (int16_t) maximum;
}

// Maximum absolute value of word32 vector. C version for generic platforms.
int32_t WebRtcSpl_MaxAbsValueW32C(const int32_t *vector, size_t length) {
    // Use uint32_t for the local variables, to accommodate the return value
    // of abs(0x80000000), which is 0x80000000.

    uint32_t absolute = 0, maximum = 0;
    size_t i = 0;

    RTC_DCHECK_GT(length, 0);

    for (i = 0; i < length; i++) {
        absolute = abs((int) vector[i]);
        if (absolute > maximum) {
            maximum = absolute;
        }
    }

    maximum = WEBRTC_SPL_MIN(maximum, WEBRTC_SPL_WORD32_MAX);

    return (int32_t) maximum;
}

// Maximum value of word16 vector. C version for generic platforms.
int16_t WebRtcSpl_MaxValueW16C(const int16_t *vector, size_t length) {
    int16_t maximum = WEBRTC_SPL_WORD16_MIN;
    size_t i = 0;

    RTC_DCHECK_GT(length, 0);

    for (i = 0; i < length; i++) {
        if (vector[i] > maximum)
            maximum = vector[i];
    }
    return maximum;
}

// Maximum value of word32 vector. C version for generic platforms.
int32_t WebRtcSpl_MaxValueW32C(const int32_t *vector, size_t length) {
    int32_t maximum = WEBRTC_SPL_WORD32_MIN;
    size_t i = 0;

    RTC_DCHECK_GT(length, 0);

    for (i = 0; i < length; i++) {
        if (vector[i] > maximum)
            maximum = vector[i];
    }
    return maximum;
}

// Minimum value of word16 vector. C version for generic platforms.
int16_t WebRtcSpl_MinValueW16C(const int16_t *vector, size_t length) {
    int16_t minimum = WEBRTC_SPL_WORD16_MAX;
    size_t i = 0;

    RTC_DCHECK_GT(length, 0);

    for (i = 0; i < length; i++) {
        if (vector[i] < minimum)
            minimum = vector[i];
    }
    return minimum;
}

// Minimum value of word32 vector. C version for generic platforms.
int32_t WebRtcSpl_MinValueW32C(const int32_t *vector, size_t length) {
    int32_t minimum = WEBRTC_SPL_WORD32_MAX;
    size_t i = 0;

    RTC_DCHECK_GT(length, 0);

    for (i = 0; i < length; i++) {
        if (vector[i] < minimum)
            minimum = vector[i];
    }
    return minimum;
}
