/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "aecm.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

static __inline int32_t WebRtcSpl_AddSatW32(int32_t a, int32_t b) {
    // Do the addition in unsigned numbers, since signed overflow is undefined
    // behavior.
    const int32_t sum = (int32_t) ((uint32_t) a + (uint32_t) b);

    // a + b can't overflow if a and b have different signs. If they have the
    // same sign, a + b also has the same sign iff it didn't overflow.
    if ((a < 0) == (b < 0) && (a < 0) != (sum < 0)) {
        // The direction of the overflow is obvious from the sign of a + b.
        return sum < 0 ? INT32_MAX : INT32_MIN;
    }
    return sum;
}

static __inline uint32_t __clz_uint32(uint32_t v) {
// Never used with input 0
    assert(v > 0);
#if defined(__INTEL_COMPILER)
    return _bit_scan_reverse(v) ^ 31U;
#elif defined(__GNUC__) && (__GNUC__ >= 4 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
// This will translate either to (bsr ^ 31U), clz , ctlz, cntlz, lzcnt depending on
// -march= setting or to a software routine in exotic machines.
    return __builtin_clz(v);
#elif defined(_MSC_VER)
    // for _BitScanReverse
#include <intrin.h>
    {
        uint32_t idx;
        _BitScanReverse(&idx, v);
        return idx ^ 31U;
    }
#else
// Will never be emitted for MSVC, GCC, Intel compilers
    static const uint8_t byte_to_unary_table[] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    return word > 0xffffff ? byte_to_unary_table[v >> 24] :
        word > 0xffff ? byte_to_unary_table[v >> 16] + 8 :
        word > 0xff ? byte_to_unary_table[v >> 8] + 16 :
        byte_to_unary_table[v] + 24;
#endif
}

// Return the number of steps a can be left-shifted without overflow,
// or 0 if a == 0.
static __inline int16_t WebRtcSpl_NormW16(int16_t a) {
    const int32_t a32 = a;
    return a == 0 ? 0 : __clz_uint32(a < 0 ? ~a32 : a32) - 17;
}

// Return the number of steps a can be left-shifted without overflow,
// or 0 if a == 0.
static __inline int16_t NormU32(uint32_t a) {

    if (a == 0) return 0;
    return (int16_t) __clz_uint32(a);
}


// Return the number of steps a can be left-shifted without overflow,
// or 0 if a == 0.
static __inline int16_t NormW32(int32_t a) {

    if (a == 0) return 0;
    uint32_t v = (uint32_t) (a < 0 ? ~a : a);
    // Returns the number of leading zero bits in the argument.
    return (int16_t) (__clz_uint32(v) - 1);
}

static __inline int16_t WebRtcSpl_SatW32ToW16(int32_t value32) {
    int16_t out16 = (int16_t) value32;

    if (value32 > 32767)
        out16 = 32767;
    else if (value32 < -32768)
        out16 = -32768;

    return out16;
}

static __inline int16_t WebRtcSpl_AddSatW16(int16_t a, int16_t b) {
    return WebRtcSpl_SatW32ToW16((int32_t) a + (int32_t) b);
}


// Return the number of steps a can be left-shifted without overflow,
// or 0 if a == 0.
static __inline int16_t WebRtcSpl_NormW32(int32_t a) {

    if (a == 0) return 0;
    uint32_t v = (uint32_t) (a < 0 ? ~a : a);
    // Returns the number of leading zero bits in the argument.
    return (int16_t) (__clz_uint32(v) - 1);
}

int32_t WebRtcSpl_DivW32W16(int32_t num, int16_t den) {
    // Guard against division with 0
    if (den != 0) {
        return (int32_t) (num / den);
    } else {
        return (int32_t) 0x7FFFFFFF;
    }
}

#ifdef AEC_DEBUG
FILE *dfile;
FILE *testfile;
#endif

const int16_t WebRtcAecm_kCosTable[] = {
        8192, 8190, 8187, 8180, 8172, 8160, 8147, 8130, 8112,
        8091, 8067, 8041, 8012, 7982, 7948, 7912, 7874, 7834,
        7791, 7745, 7697, 7647, 7595, 7540, 7483, 7424, 7362,
        7299, 7233, 7164, 7094, 7021, 6947, 6870, 6791, 6710,
        6627, 6542, 6455, 6366, 6275, 6182, 6087, 5991, 5892,
        5792, 5690, 5586, 5481, 5374, 5265, 5155, 5043, 4930,
        4815, 4698, 4580, 4461, 4341, 4219, 4096, 3971, 3845,
        3719, 3591, 3462, 3331, 3200, 3068, 2935, 2801, 2667,
        2531, 2395, 2258, 2120, 1981, 1842, 1703, 1563, 1422,
        1281, 1140, 998, 856, 713, 571, 428, 285, 142,
        0, -142, -285, -428, -571, -713, -856, -998, -1140,
        -1281, -1422, -1563, -1703, -1842, -1981, -2120, -2258, -2395,
        -2531, -2667, -2801, -2935, -3068, -3200, -3331, -3462, -3591,
        -3719, -3845, -3971, -4095, -4219, -4341, -4461, -4580, -4698,
        -4815, -4930, -5043, -5155, -5265, -5374, -5481, -5586, -5690,
        -5792, -5892, -5991, -6087, -6182, -6275, -6366, -6455, -6542,
        -6627, -6710, -6791, -6870, -6947, -7021, -7094, -7164, -7233,
        -7299, -7362, -7424, -7483, -7540, -7595, -7647, -7697, -7745,
        -7791, -7834, -7874, -7912, -7948, -7982, -8012, -8041, -8067,
        -8091, -8112, -8130, -8147, -8160, -8172, -8180, -8187, -8190,
        -8191, -8190, -8187, -8180, -8172, -8160, -8147, -8130, -8112,
        -8091, -8067, -8041, -8012, -7982, -7948, -7912, -7874, -7834,
        -7791, -7745, -7697, -7647, -7595, -7540, -7483, -7424, -7362,
        -7299, -7233, -7164, -7094, -7021, -6947, -6870, -6791, -6710,
        -6627, -6542, -6455, -6366, -6275, -6182, -6087, -5991, -5892,
        -5792, -5690, -5586, -5481, -5374, -5265, -5155, -5043, -4930,
        -4815, -4698, -4580, -4461, -4341, -4219, -4096, -3971, -3845,
        -3719, -3591, -3462, -3331, -3200, -3068, -2935, -2801, -2667,
        -2531, -2395, -2258, -2120, -1981, -1842, -1703, -1563, -1422,
        -1281, -1140, -998, -856, -713, -571, -428, -285, -142,
        0, 142, 285, 428, 571, 713, 856, 998, 1140,
        1281, 1422, 1563, 1703, 1842, 1981, 2120, 2258, 2395,
        2531, 2667, 2801, 2935, 3068, 3200, 3331, 3462, 3591,
        3719, 3845, 3971, 4095, 4219, 4341, 4461, 4580, 4698,
        4815, 4930, 5043, 5155, 5265, 5374, 5481, 5586, 5690,
        5792, 5892, 5991, 6087, 6182, 6275, 6366, 6455, 6542,
        6627, 6710, 6791, 6870, 6947, 7021, 7094, 7164, 7233,
        7299, 7362, 7424, 7483, 7540, 7595, 7647, 7697, 7745,
        7791, 7834, 7874, 7912, 7948, 7982, 8012, 8041, 8067,
        8091, 8112, 8130, 8147, 8160, 8172, 8180, 8187, 8190
};

const int16_t WebRtcAecm_kSinTable[] = {
        0, 142, 285, 428, 571, 713, 856, 998,
        1140, 1281, 1422, 1563, 1703, 1842, 1981, 2120,
        2258, 2395, 2531, 2667, 2801, 2935, 3068, 3200,
        3331, 3462, 3591, 3719, 3845, 3971, 4095, 4219,
        4341, 4461, 4580, 4698, 4815, 4930, 5043, 5155,
        5265, 5374, 5481, 5586, 5690, 5792, 5892, 5991,
        6087, 6182, 6275, 6366, 6455, 6542, 6627, 6710,
        6791, 6870, 6947, 7021, 7094, 7164, 7233, 7299,
        7362, 7424, 7483, 7540, 7595, 7647, 7697, 7745,
        7791, 7834, 7874, 7912, 7948, 7982, 8012, 8041,
        8067, 8091, 8112, 8130, 8147, 8160, 8172, 8180,
        8187, 8190, 8191, 8190, 8187, 8180, 8172, 8160,
        8147, 8130, 8112, 8091, 8067, 8041, 8012, 7982,
        7948, 7912, 7874, 7834, 7791, 7745, 7697, 7647,
        7595, 7540, 7483, 7424, 7362, 7299, 7233, 7164,
        7094, 7021, 6947, 6870, 6791, 6710, 6627, 6542,
        6455, 6366, 6275, 6182, 6087, 5991, 5892, 5792,
        5690, 5586, 5481, 5374, 5265, 5155, 5043, 4930,
        4815, 4698, 4580, 4461, 4341, 4219, 4096, 3971,
        3845, 3719, 3591, 3462, 3331, 3200, 3068, 2935,
        2801, 2667, 2531, 2395, 2258, 2120, 1981, 1842,
        1703, 1563, 1422, 1281, 1140, 998, 856, 713,
        571, 428, 285, 142, 0, -142, -285, -428,
        -571, -713, -856, -998, -1140, -1281, -1422, -1563,
        -1703, -1842, -1981, -2120, -2258, -2395, -2531, -2667,
        -2801, -2935, -3068, -3200, -3331, -3462, -3591, -3719,
        -3845, -3971, -4095, -4219, -4341, -4461, -4580, -4698,
        -4815, -4930, -5043, -5155, -5265, -5374, -5481, -5586,
        -5690, -5792, -5892, -5991, -6087, -6182, -6275, -6366,
        -6455, -6542, -6627, -6710, -6791, -6870, -6947, -7021,
        -7094, -7164, -7233, -7299, -7362, -7424, -7483, -7540,
        -7595, -7647, -7697, -7745, -7791, -7834, -7874, -7912,
        -7948, -7982, -8012, -8041, -8067, -8091, -8112, -8130,
        -8147, -8160, -8172, -8180, -8187, -8190, -8191, -8190,
        -8187, -8180, -8172, -8160, -8147, -8130, -8112, -8091,
        -8067, -8041, -8012, -7982, -7948, -7912, -7874, -7834,
        -7791, -7745, -7697, -7647, -7595, -7540, -7483, -7424,
        -7362, -7299, -7233, -7164, -7094, -7021, -6947, -6870,
        -6791, -6710, -6627, -6542, -6455, -6366, -6275, -6182,
        -6087, -5991, -5892, -5792, -5690, -5586, -5481, -5374,
        -5265, -5155, -5043, -4930, -4815, -4698, -4580, -4461,
        -4341, -4219, -4096, -3971, -3845, -3719, -3591, -3462,
        -3331, -3200, -3068, -2935, -2801, -2667, -2531, -2395,
        -2258, -2120, -1981, -1842, -1703, -1563, -1422, -1281,
        -1140, -998, -856, -713, -571, -428, -285, -142
};

// Initialization table for echo channel in 8 kHz
static const int16_t kChannelStored8kHz[PART_LEN1] = {
        2040, 1815, 1590, 1498, 1405, 1395, 1385, 1418,
        1451, 1506, 1562, 1644, 1726, 1804, 1882, 1918,
        1953, 1982, 2010, 2025, 2040, 2034, 2027, 2021,
        2014, 1997, 1980, 1925, 1869, 1800, 1732, 1683,
        1635, 1604, 1572, 1545, 1517, 1481, 1444, 1405,
        1367, 1331, 1294, 1270, 1245, 1239, 1233, 1247,
        1260, 1282, 1303, 1338, 1373, 1407, 1441, 1470,
        1499, 1524, 1549, 1565, 1582, 1601, 1621, 1649,
        1676
};

// Initialization table for echo channel in 16 kHz
static const int16_t kChannelStored16kHz[PART_LEN1] = {
        2040, 1590, 1405, 1385, 1451, 1562, 1726, 1882,
        1953, 2010, 2040, 2027, 2014, 1980, 1869, 1732,
        1635, 1572, 1517, 1444, 1367, 1294, 1245, 1233,
        1260, 1303, 1373, 1441, 1499, 1549, 1582, 1621,
        1676, 1741, 1802, 1861, 1921, 1983, 2040, 2102,
        2170, 2265, 2375, 2515, 2651, 2781, 2922, 3075,
        3253, 3471, 3738, 3976, 4151, 4258, 4308, 4288,
        4270, 4253, 4237, 4179, 4086, 3947, 3757, 3484,
        3153
};

// Moves the pointer to the next entry and inserts |far_spectrum| and
// corresponding Q-domain in its buffer.
//
// Inputs:
//      - self          : Pointer to the delay estimation instance
//      - far_spectrum  : Pointer to the far end spectrum
//      - far_q         : Q-domain of far end spectrum
//
void WebRtcAecm_UpdateFarHistory(AecmCore *self,
                                 uint16_t *far_spectrum,
                                 int far_q) {
    // Get new buffer position
    self->far_history_pos++;
    if (self->far_history_pos >= MAX_DELAY) {
        self->far_history_pos = 0;
    }
    // Update Q-domain buffer
    self->far_q_domains[self->far_history_pos] = far_q;
    // Update far end spectrum buffer
    memcpy(&(self->far_history[self->far_history_pos * PART_LEN1]),
           far_spectrum,
           sizeof(uint16_t) * PART_LEN1);
}

// Returns a pointer to the far end spectrum aligned to current near end
// spectrum. The function WebRtc_DelayEstimatorProcessFix(...) should have been
// called before AlignedFarend(...). Otherwise, you get the pointer to the
// previous frame. The memory is only valid until the next call of
// WebRtc_DelayEstimatorProcessFix(...).
//
// Inputs:
//      - self              : Pointer to the AECM instance.
//      - delay             : Current delay estimate.
//
// Output:
//      - far_q             : The Q-domain of the aligned far end spectrum
//
// Return value:
//      - far_spectrum      : Pointer to the aligned far end spectrum
//                            NULL - Error
//
const uint16_t *WebRtcAecm_AlignedFarend(AecmCore *self,
                                         int *far_q,
                                         int delay) {
    int buffer_position = 0;
    assert(self);
    buffer_position = self->far_history_pos - delay;

    // Check buffer position
    if (buffer_position < 0) {
        buffer_position += MAX_DELAY;
    }
    // Get Q-domain
    *far_q = self->far_q_domains[buffer_position];
    // Return far end spectrum
    return &(self->far_history[buffer_position * PART_LEN1]);
}

// Declare function pointers.
CalcLinearEnergies WebRtcAecm_CalcLinearEnergies;
StoreAdaptiveChannel WebRtcAecm_StoreAdaptiveChannel;
ResetAdaptiveChannel WebRtcAecm_ResetAdaptiveChannel;

AecmCore *WebRtcAecm_CreateCore() {
    AecmCore *aecm = (AecmCore *) (malloc(sizeof(AecmCore)));

    aecm->farFrameBuf = WebRtc_CreateBuffer(FRAME_LEN + PART_LEN,
                                            sizeof(int16_t));
    if (!aecm->farFrameBuf) {
        WebRtcAecm_FreeCore(aecm);
        return NULL;
    }

    aecm->nearNoisyFrameBuf = WebRtc_CreateBuffer(FRAME_LEN + PART_LEN,
                                                  sizeof(int16_t));
    if (!aecm->nearNoisyFrameBuf) {
        WebRtcAecm_FreeCore(aecm);
        return NULL;
    }

    aecm->nearCleanFrameBuf = WebRtc_CreateBuffer(FRAME_LEN + PART_LEN,
                                                  sizeof(int16_t));
    if (!aecm->nearCleanFrameBuf) {
        WebRtcAecm_FreeCore(aecm);
        return NULL;
    }

    aecm->outFrameBuf = WebRtc_CreateBuffer(FRAME_LEN + PART_LEN,
                                            sizeof(int16_t));
    if (!aecm->outFrameBuf) {
        WebRtcAecm_FreeCore(aecm);
        return NULL;
    }

    aecm->delay_estimator_farend = WebRtc_CreateDelayEstimatorFarend(PART_LEN1,
                                                                     MAX_DELAY);
    if (aecm->delay_estimator_farend == NULL) {
        WebRtcAecm_FreeCore(aecm);
        return NULL;
    }
    aecm->delay_estimator =
            WebRtc_CreateDelayEstimator(aecm->delay_estimator_farend, 0);
    if (aecm->delay_estimator == NULL) {
        WebRtcAecm_FreeCore(aecm);
        return NULL;
    }
    // TODO(bjornv): Explicitly disable robust delay validation until no
    // performance regression has been established.  Then remove the line.
    WebRtc_enable_robust_validation(aecm->delay_estimator, 0);

    aecm->real_fft = WebRtcSpl_CreateRealFFT(PART_LEN_SHIFT);
    if (aecm->real_fft == NULL) {
        WebRtcAecm_FreeCore(aecm);
        return NULL;
    }

    // Init some aecm pointers. 16 and 32 byte alignment is only necessary
    // for Neon code currently.
    aecm->xBuf = (int16_t *) (((uintptr_t) aecm->xBuf_buf + 31) & ~31);
    aecm->dBufClean = (int16_t *) (((uintptr_t) aecm->dBufClean_buf + 31) & ~31);
    aecm->dBufNoisy = (int16_t *) (((uintptr_t) aecm->dBufNoisy_buf + 31) & ~31);
    aecm->outBuf = (int16_t *) (((uintptr_t) aecm->outBuf_buf + 15) & ~15);
    aecm->channelStored = (int16_t *) (((uintptr_t)
                                                aecm->channelStored_buf + 15) & ~15);
    aecm->channelAdapt16 = (int16_t *) (((uintptr_t)
                                                 aecm->channelAdapt16_buf + 15) & ~15);
    aecm->channelAdapt32 = (int32_t *) (((uintptr_t)
                                                 aecm->channelAdapt32_buf + 31) & ~31);

    return aecm;
}

void WebRtcAecm_InitEchoPathCore(AecmCore *aecm, const int16_t *echo_path) {
    int i = 0;

    // Reset the stored channel
    memcpy(aecm->channelStored, echo_path, sizeof(int16_t) * PART_LEN1);
    // Reset the adapted channels
    memcpy(aecm->channelAdapt16, echo_path, sizeof(int16_t) * PART_LEN1);
    for (i = 0; i < PART_LEN1; i++) {
        aecm->channelAdapt32[i] = (int32_t) aecm->channelAdapt16[i] << 16;
    }

    // Reset channel storing variables
    aecm->mseAdaptOld = 1000;
    aecm->mseStoredOld = 1000;
    aecm->mseThreshold = (int32_t) 0x7fffffff;
    aecm->mseChannelCount = 0;
}

static void CalcLinearEnergiesC(AecmCore *aecm,
                                const uint16_t *far_spectrum,
                                int32_t *echo_est,
                                uint32_t *far_energy,
                                uint32_t *echo_energy_adapt,
                                uint32_t *echo_energy_stored) {
    int i;

    // Get energy for the delayed far end signal and estimated
    // echo using both stored and adapted channels.
    for (i = 0; i < PART_LEN1; i++) {
        echo_est[i] = WEBRTC_SPL_MUL_16_U16(aecm->channelStored[i],
                                            far_spectrum[i]);
        (*far_energy) += (uint32_t) (far_spectrum[i]);
        *echo_energy_adapt += aecm->channelAdapt16[i] * far_spectrum[i];
        (*echo_energy_stored) += (uint32_t) echo_est[i];
    }
}

static void StoreAdaptiveChannelC(AecmCore *aecm,
                                  const uint16_t *far_spectrum,
                                  int32_t *echo_est) {
    int i;

    // During startup we store the channel every block.
    memcpy(aecm->channelStored, aecm->channelAdapt16, sizeof(int16_t) * PART_LEN1);
    // Recalculate echo estimate
    for (i = 0; i < PART_LEN; i += 4) {
        echo_est[i] = WEBRTC_SPL_MUL_16_U16(aecm->channelStored[i],
                                            far_spectrum[i]);
        echo_est[i + 1] = WEBRTC_SPL_MUL_16_U16(aecm->channelStored[i + 1],
                                                far_spectrum[i + 1]);
        echo_est[i + 2] = WEBRTC_SPL_MUL_16_U16(aecm->channelStored[i + 2],
                                                far_spectrum[i + 2]);
        echo_est[i + 3] = WEBRTC_SPL_MUL_16_U16(aecm->channelStored[i + 3],
                                                far_spectrum[i + 3]);
    }
    echo_est[i] = WEBRTC_SPL_MUL_16_U16(aecm->channelStored[i],
                                        far_spectrum[i]);
}

static void ResetAdaptiveChannelC(AecmCore *aecm) {
    int i;

    // The stored channel has a significantly lower MSE than the adaptive one for
    // two consecutive calculations. Reset the adaptive channel.
    memcpy(aecm->channelAdapt16, aecm->channelStored,
           sizeof(int16_t) * PART_LEN1);
    // Restore the W32 channel
    for (i = 0; i < PART_LEN; i += 4) {
        aecm->channelAdapt32[i] = (int32_t) aecm->channelStored[i] << 16;
        aecm->channelAdapt32[i + 1] = (int32_t) aecm->channelStored[i + 1] << 16;
        aecm->channelAdapt32[i + 2] = (int32_t) aecm->channelStored[i + 2] << 16;
        aecm->channelAdapt32[i + 3] = (int32_t) aecm->channelStored[i + 3] << 16;
    }
    aecm->channelAdapt32[i] = (int32_t) aecm->channelStored[i] << 16;
}

// Initialize function pointers for ARM Neon platform.
#if defined(WEBRTC_HAS_NEON)
static void WebRtcAecm_InitNeon(void)
{
  WebRtcAecm_StoreAdaptiveChannel = WebRtcAecm_StoreAdaptiveChannelNeon;
  WebRtcAecm_ResetAdaptiveChannel = WebRtcAecm_ResetAdaptiveChannelNeon;
  WebRtcAecm_CalcLinearEnergies = WebRtcAecm_CalcLinearEnergiesNeon;
}
#endif

// Initialize function pointers for MIPS platform.
#if defined(MIPS32_LE)
static void WebRtcAecm_InitMips(void)
{
#if defined(MIPS_DSP_R1_LE)
  WebRtcAecm_StoreAdaptiveChannel = WebRtcAecm_StoreAdaptiveChannel_mips;
  WebRtcAecm_ResetAdaptiveChannel = WebRtcAecm_ResetAdaptiveChannel_mips;
#endif
  WebRtcAecm_CalcLinearEnergies = WebRtcAecm_CalcLinearEnergies_mips;
}
#endif

// WebRtcAecm_InitCore(...)
//
// This function initializes the AECM instant created with WebRtcAecm_CreateCore(...)
// Input:
//      - aecm            : Pointer to the Echo Suppression instance
//      - samplingFreq   : Sampling Frequency
//
// Output:
//      - aecm            : Initialized instance
//
// Return value         :  0 - Ok
//                        -1 - Error
//
int WebRtcAecm_InitCore(AecmCore *const aecm, int samplingFreq) {
    int i = 0;
    int32_t tmp32 = PART_LEN1 * PART_LEN1;
    int16_t tmp16 = PART_LEN1;

    if (samplingFreq != 8000 && samplingFreq != 16000) {
        return -1;
    }
    // sanity check of sampling frequency
    aecm->mult = (int16_t) samplingFreq / 8000;

    aecm->farBufWritePos = 0;
    aecm->farBufReadPos = 0;
    aecm->knownDelay = 0;
    aecm->lastKnownDelay = 0;

    WebRtc_InitBuffer(aecm->farFrameBuf);
    WebRtc_InitBuffer(aecm->nearNoisyFrameBuf);
    WebRtc_InitBuffer(aecm->nearCleanFrameBuf);
    WebRtc_InitBuffer(aecm->outFrameBuf);

    memset(aecm->xBuf_buf, 0, sizeof(aecm->xBuf_buf));
    memset(aecm->dBufClean_buf, 0, sizeof(aecm->dBufClean_buf));
    memset(aecm->dBufNoisy_buf, 0, sizeof(aecm->dBufNoisy_buf));
    memset(aecm->outBuf_buf, 0, sizeof(aecm->outBuf_buf));

    aecm->seed = 666;
    aecm->totCount = 0;

    if (WebRtc_InitDelayEstimatorFarend(aecm->delay_estimator_farend) != 0) {
        return -1;
    }
    if (WebRtc_InitDelayEstimator(aecm->delay_estimator) != 0) {
        return -1;
    }
    // Set far end histories to zero
    memset(aecm->far_history, 0, sizeof(uint16_t) * PART_LEN1 * MAX_DELAY);
    memset(aecm->far_q_domains, 0, sizeof(int) * MAX_DELAY);
    aecm->far_history_pos = MAX_DELAY;

    aecm->nlpFlag = 1;
    aecm->fixedDelay = -1;

    aecm->dfaCleanQDomain = 0;
    aecm->dfaCleanQDomainOld = 0;
    aecm->dfaNoisyQDomain = 0;
    aecm->dfaNoisyQDomainOld = 0;

    memset(aecm->nearLogEnergy, 0, sizeof(aecm->nearLogEnergy));
    aecm->farLogEnergy = 0;
    memset(aecm->echoAdaptLogEnergy, 0, sizeof(aecm->echoAdaptLogEnergy));
    memset(aecm->echoStoredLogEnergy, 0, sizeof(aecm->echoStoredLogEnergy));

    // Initialize the echo channels with a stored shape.
    if (samplingFreq == 8000) {
        WebRtcAecm_InitEchoPathCore(aecm, kChannelStored8kHz);
    } else {
        WebRtcAecm_InitEchoPathCore(aecm, kChannelStored16kHz);
    }

    memset(aecm->echoFilt, 0, sizeof(aecm->echoFilt));
    memset(aecm->nearFilt, 0, sizeof(aecm->nearFilt));
    aecm->noiseEstCtr = 0;

    aecm->cngMode = AecmTrue;

    memset(aecm->noiseEstTooLowCtr, 0, sizeof(aecm->noiseEstTooLowCtr));
    memset(aecm->noiseEstTooHighCtr, 0, sizeof(aecm->noiseEstTooHighCtr));
    // Shape the initial noise level to an approximate pink noise.
    for (i = 0; i < (PART_LEN1 >> 1) - 1; i++) {
        aecm->noiseEst[i] = (tmp32 << 8);
        tmp16--;
        tmp32 -= (int32_t) ((tmp16 << 1) + 1);
    }
    for (; i < PART_LEN1; i++) {
        aecm->noiseEst[i] = (tmp32 << 8);
    }

    aecm->farEnergyMin = 32767;
    aecm->farEnergyMax = -32768;
    aecm->farEnergyMaxMin = 0;
    aecm->farEnergyVAD = FAR_ENERGY_MIN; // This prevents false speech detection at the
    // beginning.
    aecm->farEnergyMSE = 0;
    aecm->currentVADValue = 0;
    aecm->vadUpdateCount = 0;
    aecm->firstVAD = 1;

    aecm->startupState = 0;
    aecm->supGain = SUPGAIN_DEFAULT;
    aecm->supGainOld = SUPGAIN_DEFAULT;

    aecm->supGainErrParamA = SUPGAIN_ERROR_PARAM_A;
    aecm->supGainErrParamD = SUPGAIN_ERROR_PARAM_D;
    aecm->supGainErrParamDiffAB = SUPGAIN_ERROR_PARAM_A - SUPGAIN_ERROR_PARAM_B;
    aecm->supGainErrParamDiffBD = SUPGAIN_ERROR_PARAM_B - SUPGAIN_ERROR_PARAM_D;

    // Assert a preprocessor definition at compile-time. It's an assumption
    // used in assembly code, so check the assembly files before any change.
    static_assert(PART_LEN % 16 == 0, "PART_LEN is not a multiple of 16");

    // Initialize function pointers.
    WebRtcAecm_CalcLinearEnergies = CalcLinearEnergiesC;
    WebRtcAecm_StoreAdaptiveChannel = StoreAdaptiveChannelC;
    WebRtcAecm_ResetAdaptiveChannel = ResetAdaptiveChannelC;

#if defined(WEBRTC_HAS_NEON)
    WebRtcAecm_InitNeon();
#endif

#if defined(MIPS32_LE)
    WebRtcAecm_InitMips();
#endif
    return 0;
}

// TODO(bjornv): This function is currently not used. Add support for these
// parameters from a higher level
int WebRtcAecm_Control(AecmCore *aecm, int delay, int nlpFlag) {
    aecm->nlpFlag = nlpFlag;
    aecm->fixedDelay = delay;

    return 0;
}

void WebRtcAecm_FreeCore(AecmCore *aecm) {
    if (aecm == NULL) {
        return;
    }

    WebRtc_FreeBuffer(aecm->farFrameBuf);
    WebRtc_FreeBuffer(aecm->nearNoisyFrameBuf);
    WebRtc_FreeBuffer(aecm->nearCleanFrameBuf);
    WebRtc_FreeBuffer(aecm->outFrameBuf);

    WebRtc_FreeDelayEstimator(aecm->delay_estimator);
    WebRtc_FreeDelayEstimatorFarend(aecm->delay_estimator_farend);
    WebRtcSpl_FreeRealFFT(aecm->real_fft);

    free(aecm);
}

int WebRtcAecm_ProcessFrame(AecmCore *aecm,
                            const int16_t *farend,
                            const int16_t *nearendNoisy,
                            const int16_t *nearendClean,
                            int16_t *out) {
    int16_t outBlock_buf[PART_LEN + 8]; // Align buffer to 8-byte boundary.
    int16_t *outBlock = (int16_t *) (((uintptr_t) outBlock_buf + 15) & ~15);

    int16_t farFrame[FRAME_LEN];
    const int16_t *out_ptr = NULL;
    int size = 0;

    // Buffer the current frame.
    // Fetch an older one corresponding to the delay.
    WebRtcAecm_BufferFarFrame(aecm, farend, FRAME_LEN);
    WebRtcAecm_FetchFarFrame(aecm, farFrame, FRAME_LEN, aecm->knownDelay);

    // Buffer the synchronized far and near frames,
    // to pass the smaller blocks individually.
    WebRtc_WriteBuffer(aecm->farFrameBuf, farFrame, FRAME_LEN);
    WebRtc_WriteBuffer(aecm->nearNoisyFrameBuf, nearendNoisy, FRAME_LEN);
    if (nearendClean != NULL) {
        WebRtc_WriteBuffer(aecm->nearCleanFrameBuf, nearendClean, FRAME_LEN);
    }

    // Process as many blocks as possible.
    while (WebRtc_available_read(aecm->farFrameBuf) >= PART_LEN) {
        int16_t far_block[PART_LEN];
        const int16_t *far_block_ptr = NULL;
        int16_t near_noisy_block[PART_LEN];
        const int16_t *near_noisy_block_ptr = NULL;

        WebRtc_ReadBuffer(aecm->farFrameBuf, (void **) &far_block_ptr, far_block,
                          PART_LEN);
        WebRtc_ReadBuffer(aecm->nearNoisyFrameBuf,
                          (void **) &near_noisy_block_ptr,
                          near_noisy_block,
                          PART_LEN);
        if (nearendClean != NULL) {
            int16_t near_clean_block[PART_LEN];
            const int16_t *near_clean_block_ptr = NULL;

            WebRtc_ReadBuffer(aecm->nearCleanFrameBuf,
                              (void **) &near_clean_block_ptr,
                              near_clean_block,
                              PART_LEN);
            if (WebRtcAecm_ProcessBlock(aecm,
                                        far_block_ptr,
                                        near_noisy_block_ptr,
                                        near_clean_block_ptr,
                                        outBlock) == -1) {
                return -1;
            }
        } else {
            if (WebRtcAecm_ProcessBlock(aecm,
                                        far_block_ptr,
                                        near_noisy_block_ptr,
                                        NULL,
                                        outBlock) == -1) {
                return -1;
            }
        }

        WebRtc_WriteBuffer(aecm->outFrameBuf, outBlock, PART_LEN);
    }

    // Stuff the out buffer if we have less than a frame to output.
    // This should only happen for the first frame.
    size = (int) WebRtc_available_read(aecm->outFrameBuf);
    if (size < FRAME_LEN) {
        WebRtc_MoveReadPtr(aecm->outFrameBuf, size - FRAME_LEN);
    }

    // Obtain an output frame.
    WebRtc_ReadBuffer(aecm->outFrameBuf, (void **) &out_ptr, out, FRAME_LEN);
    if (out_ptr != out) {
        // ReadBuffer() hasn't copied to |out| in this case.
        memcpy(out, out_ptr, FRAME_LEN * sizeof(int16_t));
    }

    return 0;
}

// WebRtcAecm_AsymFilt(...)
//
// Performs asymmetric filtering.
//
// Inputs:
//      - filtOld       : Previous filtered value.
//      - inVal         : New input value.
//      - stepSizePos   : Step size when we have a positive contribution.
//      - stepSizeNeg   : Step size when we have a negative contribution.
//
// Output:
//
// Return: - Filtered value.
//
int16_t WebRtcAecm_AsymFilt(const int16_t filtOld, const int16_t inVal,
                            const int16_t stepSizePos,
                            const int16_t stepSizeNeg) {
    int16_t retVal;

    if ((filtOld == 32767) | (filtOld == -32768)) {
        return inVal;
    }
    retVal = filtOld;
    if (filtOld > inVal) {
        retVal -= (filtOld - inVal) >> stepSizeNeg;
    } else {
        retVal += (inVal - filtOld) >> stepSizePos;
    }

    return retVal;
}

// ExtractFractionPart(a, zeros)
//
// returns the fraction part of |a|, with |zeros| number of leading zeros, as an
// int16_t scaled to Q8. There is no sanity check of |a| in the sense that the
// number of zeros match.
static int16_t ExtractFractionPart(uint32_t a, int zeros) {
    return (int16_t) (((a << zeros) & 0x7FFFFFFF) >> 23);
}

// Calculates and returns the log of |energy| in Q8. The input |energy| is
// supposed to be in Q(|q_domain|).
static int16_t LogOfEnergyInQ8(uint32_t energy, int q_domain) {
    static const int16_t kLogLowValue = PART_LEN_SHIFT << 7;
    int16_t log_energy_q8 = kLogLowValue;
    if (energy > 0) {
        int zeros = NormU32(energy);
        int16_t frac = ExtractFractionPart(energy, zeros);
        // log2 of |energy| in Q8.
        log_energy_q8 += ((31 - zeros) << 8) + frac - (q_domain << 8);
    }
    return log_energy_q8;
}

// WebRtcAecm_CalcEnergies(...)
//
// This function calculates the log of energies for nearend, farend and estimated
// echoes. There is also an update of energy decision levels, i.e. internal VAD.
//
//
// @param  aecm         [i/o]   Handle of the AECM instance.
// @param  far_spectrum [in]    Pointer to farend spectrum.
// @param  far_q        [in]    Q-domain of farend spectrum.
// @param  nearEner     [in]    Near end energy for current block in
//                              Q(aecm->dfaQDomain).
// @param  echoEst      [out]   Estimated echo in Q(xfa_q+RESOLUTION_CHANNEL16).
//
void WebRtcAecm_CalcEnergies(AecmCore *aecm,
                             const uint16_t *far_spectrum,
                             const int16_t far_q,
                             const uint32_t nearEner,
                             int32_t *echoEst) {
    // Local variables
    uint32_t tmpAdapt = 0;
    uint32_t tmpStored = 0;
    uint32_t tmpFar = 0;

    int i;

    int16_t tmp16;
    int16_t increase_max_shifts = 4;
    int16_t decrease_max_shifts = 11;
    int16_t increase_min_shifts = 11;
    int16_t decrease_min_shifts = 3;

    // Get log of near end energy and store in buffer

    // Shift buffer
    memmove(aecm->nearLogEnergy + 1, aecm->nearLogEnergy,
            sizeof(int16_t) * (MAX_BUF_LEN - 1));

    // Logarithm of integrated magnitude spectrum (nearEner)
    aecm->nearLogEnergy[0] = LogOfEnergyInQ8(nearEner, aecm->dfaNoisyQDomain);

    WebRtcAecm_CalcLinearEnergies(aecm, far_spectrum, echoEst, &tmpFar, &tmpAdapt, &tmpStored);

    // Shift buffers
    memmove(aecm->echoAdaptLogEnergy + 1, aecm->echoAdaptLogEnergy,
            sizeof(int16_t) * (MAX_BUF_LEN - 1));
    memmove(aecm->echoStoredLogEnergy + 1, aecm->echoStoredLogEnergy,
            sizeof(int16_t) * (MAX_BUF_LEN - 1));

    // Logarithm of delayed far end energy
    aecm->farLogEnergy = LogOfEnergyInQ8(tmpFar, far_q);

    // Logarithm of estimated echo energy through adapted channel
    aecm->echoAdaptLogEnergy[0] = LogOfEnergyInQ8(tmpAdapt,
                                                  RESOLUTION_CHANNEL16 + far_q);

    // Logarithm of estimated echo energy through stored channel
    aecm->echoStoredLogEnergy[0] =
            LogOfEnergyInQ8(tmpStored, RESOLUTION_CHANNEL16 + far_q);

    // Update farend energy levels (min, max, vad, mse)
    if (aecm->farLogEnergy > FAR_ENERGY_MIN) {
        if (aecm->startupState == 0) {
            increase_max_shifts = 2;
            decrease_min_shifts = 2;
            increase_min_shifts = 8;
        }

        aecm->farEnergyMin = WebRtcAecm_AsymFilt(aecm->farEnergyMin, aecm->farLogEnergy,
                                                 increase_min_shifts, decrease_min_shifts);
        aecm->farEnergyMax = WebRtcAecm_AsymFilt(aecm->farEnergyMax, aecm->farLogEnergy,
                                                 increase_max_shifts, decrease_max_shifts);
        aecm->farEnergyMaxMin = (aecm->farEnergyMax - aecm->farEnergyMin);

        // Dynamic VAD region size
        tmp16 = 2560 - aecm->farEnergyMin;
        if (tmp16 > 0) {
            tmp16 = (int16_t) ((tmp16 * FAR_ENERGY_VAD_REGION) >> 9);
        } else {
            tmp16 = 0;
        }
        tmp16 += FAR_ENERGY_VAD_REGION;

        if ((aecm->startupState == 0) | (aecm->vadUpdateCount > 1024)) {
            // In startup phase or VAD update halted
            aecm->farEnergyVAD = aecm->farEnergyMin + tmp16;
        } else {
            if (aecm->farEnergyVAD > aecm->farLogEnergy) {
                aecm->farEnergyVAD +=
                        (aecm->farLogEnergy + tmp16 - aecm->farEnergyVAD) >> 6;
                aecm->vadUpdateCount = 0;
            } else {
                aecm->vadUpdateCount++;
            }
        }
        // Put MSE threshold higher than VAD
        aecm->farEnergyMSE = aecm->farEnergyVAD + (1 << 8);
    }

    // Update VAD variables
    if (aecm->farLogEnergy > aecm->farEnergyVAD) {
        if ((aecm->startupState == 0) | (aecm->farEnergyMaxMin > FAR_ENERGY_DIFF)) {
            // We are in startup or have significant dynamics in input speech level
            aecm->currentVADValue = 1;
        }
    } else {
        aecm->currentVADValue = 0;
    }
    if ((aecm->currentVADValue) && (aecm->firstVAD)) {
        aecm->firstVAD = 0;
        if (aecm->echoAdaptLogEnergy[0] > aecm->nearLogEnergy[0]) {
            // The estimated echo has higher energy than the near end signal.
            // This means that the initialization was too aggressive. Scale
            // down by a factor 8
            for (i = 0; i < PART_LEN1; i++) {
                aecm->channelAdapt16[i] >>= 3;
            }
            // Compensate the adapted echo energy level accordingly.
            aecm->echoAdaptLogEnergy[0] -= (3 << 8);
            aecm->firstVAD = 1;
        }
    }
}

// WebRtcAecm_CalcStepSize(...)
//
// This function calculates the step size used in channel estimation
//
//
// @param  aecm  [in]    Handle of the AECM instance.
// @param  mu    [out]   (Return value) Stepsize in log2(), i.e. number of shifts.
//
//
int16_t WebRtcAecm_CalcStepSize(AecmCore *const aecm) {
    int32_t tmp32;
    int16_t tmp16;
    int16_t mu = MU_MAX;

    // Here we calculate the step size mu used in the
    // following NLMS based Channel estimation algorithm
    if (!aecm->currentVADValue) {
        // Far end energy level too low, no channel update
        mu = 0;
    } else if (aecm->startupState > 0) {
        if (aecm->farEnergyMin >= aecm->farEnergyMax) {
            mu = MU_MIN;
        } else {
            tmp16 = (aecm->farLogEnergy - aecm->farEnergyMin);
            tmp32 = tmp16 * MU_DIFF;
            tmp32 = WebRtcSpl_DivW32W16(tmp32, aecm->farEnergyMaxMin);
            mu = MU_MIN - 1 - (int16_t) (tmp32);
            // The -1 is an alternative to rounding. This way we get a larger
            // stepsize, so we in some sense compensate for truncation in NLMS
        }
        if (mu < MU_MAX) {
            mu = MU_MAX; // Equivalent with maximum step size of 2^-MU_MAX
        }
    }

    return mu;
}

// WebRtcAecm_UpdateChannel(...)
//
// This function performs channel estimation. NLMS and decision on channel storage.
//
//
// @param  aecm         [i/o]   Handle of the AECM instance.
// @param  far_spectrum [in]    Absolute value of the farend signal in Q(far_q)
// @param  far_q        [in]    Q-domain of the farend signal
// @param  dfa          [in]    Absolute value of the nearend signal (Q[aecm->dfaQDomain])
// @param  mu           [in]    NLMS step size.
// @param  echoEst      [i/o]   Estimated echo in Q(far_q+RESOLUTION_CHANNEL16).
//
void WebRtcAecm_UpdateChannel(AecmCore *aecm,
                              const uint16_t *far_spectrum,
                              const int16_t far_q,
                              const uint16_t *const dfa,
                              const int16_t mu,
                              int32_t *echoEst) {
    uint32_t tmpU32no1, tmpU32no2;
    int32_t tmp32no1, tmp32no2;
    int32_t mseStored;
    int32_t mseAdapt;

    int i;

    int16_t zerosFar, zerosNum, zerosCh, zerosDfa;
    int16_t shiftChFar, shiftNum, shift2ResChan;
    int16_t tmp16no1;
    int16_t xfaQ, dfaQ;

    // This is the channel estimation algorithm. It is base on NLMS but has a variable step
    // length, which was calculated above.
    if (mu) {
        for (i = 0; i < PART_LEN1; i++) {
            // Determine norm of channel and farend to make sure we don't get overflow in
            // multiplication
            zerosCh = NormU32(aecm->channelAdapt32[i]);
            zerosFar = NormU32((uint32_t) far_spectrum[i]);
            if (zerosCh + zerosFar > 31) {
                // Multiplication is safe
                tmpU32no1 = WEBRTC_SPL_UMUL_32_16(aecm->channelAdapt32[i],
                                                  far_spectrum[i]);
                shiftChFar = 0;
            } else {
                // We need to shift down before multiplication
                shiftChFar = 32 - zerosCh - zerosFar;
                // If zerosCh == zerosFar == 0, shiftChFar is 32. A
                // right shift of 32 is undefined. To avoid that, we
                // do this check.
                tmpU32no1 = (uint32_t) (
                        shiftChFar >= 32
                        ? 0
                        : aecm->channelAdapt32[i] >> shiftChFar) *
                            far_spectrum[i];
            }
            // Determine Q-domain of numerator
            zerosNum = NormU32(tmpU32no1);
            if (dfa[i]) {
                zerosDfa = NormU32((uint32_t) dfa[i]);
            } else {
                zerosDfa = 32;
            }
            tmp16no1 = zerosDfa - 2 + aecm->dfaNoisyQDomain -
                       RESOLUTION_CHANNEL32 - far_q + shiftChFar;
            if (zerosNum > tmp16no1 + 1) {
                xfaQ = tmp16no1;
                dfaQ = zerosDfa - 2;
            } else {
                xfaQ = zerosNum - 2;
                dfaQ = RESOLUTION_CHANNEL32 + far_q - aecm->dfaNoisyQDomain -
                       shiftChFar + xfaQ;
            }
            // Add in the same Q-domain
            tmpU32no1 = WEBRTC_SPL_SHIFT_W32(tmpU32no1, xfaQ);
            tmpU32no2 = WEBRTC_SPL_SHIFT_W32((uint32_t) dfa[i], dfaQ);
            tmp32no1 = (int32_t) tmpU32no2 - (int32_t) tmpU32no1;
            zerosNum = NormW32(tmp32no1);
            if ((tmp32no1) && (far_spectrum[i] > (CHANNEL_VAD << far_q))) {
                //
                // Update is needed
                //
                // This is what we would like to compute
                //
                // tmp32no1 = dfa[i] - (aecm->channelAdapt[i] * far_spectrum[i])
                // tmp32norm = (i + 1)
                // aecm->channelAdapt[i] += (2^mu) * tmp32no1
                //                        / (tmp32norm * far_spectrum[i])
                //

                // Make sure we don't get overflow in multiplication.
                if (zerosNum + zerosFar > 31) {
                    if (tmp32no1 > 0) {
                        tmp32no2 = (int32_t) WEBRTC_SPL_UMUL_32_16(tmp32no1,
                                                                   far_spectrum[i]);
                    } else {
                        tmp32no2 = -(int32_t) WEBRTC_SPL_UMUL_32_16(-tmp32no1,
                                                                    far_spectrum[i]);
                    }
                    shiftNum = 0;
                } else {
                    shiftNum = 32 - (zerosNum + zerosFar);
                    if (tmp32no1 > 0) {
                        tmp32no2 = (tmp32no1 >> shiftNum) * far_spectrum[i];
                    } else {
                        tmp32no2 = -((-tmp32no1 >> shiftNum) * far_spectrum[i]);
                    }
                }
                // Normalize with respect to frequency bin
                tmp32no2 = WebRtcSpl_DivW32W16(tmp32no2, i + 1);
                // Make sure we are in the right Q-domain
                shift2ResChan = shiftNum + shiftChFar - xfaQ - mu - ((30 - zerosFar) << 1);
                if (NormW32(tmp32no2) < shift2ResChan) {
                    tmp32no2 = (int32_t) 0x7fffffff;
                } else {
                    tmp32no2 = WEBRTC_SPL_SHIFT_W32(tmp32no2, shift2ResChan);
                }
                aecm->channelAdapt32[i] =
                        WebRtcSpl_AddSatW32(aecm->channelAdapt32[i], tmp32no2);
                if (aecm->channelAdapt32[i] < 0) {
                    // We can never have negative channel gain
                    aecm->channelAdapt32[i] = 0;
                }
                aecm->channelAdapt16[i] =
                        (int16_t) (aecm->channelAdapt32[i] >> 16);
            }
        }
    }
    // END: Adaptive channel update

    // Determine if we should store or restore the channel
    if ((aecm->startupState == 0) & (aecm->currentVADValue)) {
        // During startup we store the channel every block,
        // and we recalculate echo estimate
        WebRtcAecm_StoreAdaptiveChannel(aecm, far_spectrum, echoEst);
    } else {
        if (aecm->farLogEnergy < aecm->farEnergyMSE) {
            aecm->mseChannelCount = 0;
        } else {
            aecm->mseChannelCount++;
        }
        // Enough data for validation. Store channel if we can.
        if (aecm->mseChannelCount >= (MIN_MSE_COUNT + 10)) {
            // We have enough data.
            // Calculate MSE of "Adapt" and "Stored" versions.
            // It is actually not MSE, but average absolute error.
            mseStored = 0;
            mseAdapt = 0;
            for (i = 0; i < MIN_MSE_COUNT; i++) {
                tmp32no1 = ((int32_t) aecm->echoStoredLogEnergy[i]
                            - (int32_t) aecm->nearLogEnergy[i]);
                tmp32no2 = WEBRTC_SPL_ABS_W32(tmp32no1);
                mseStored += tmp32no2;

                tmp32no1 = ((int32_t) aecm->echoAdaptLogEnergy[i]
                            - (int32_t) aecm->nearLogEnergy[i]);
                tmp32no2 = WEBRTC_SPL_ABS_W32(tmp32no1);
                mseAdapt += tmp32no2;
            }
            if (((mseStored << MSE_RESOLUTION) < (MIN_MSE_DIFF * mseAdapt))
                & ((aecm->mseStoredOld << MSE_RESOLUTION) < (MIN_MSE_DIFF
                                                             * aecm->mseAdaptOld))) {
                // The stored channel has a significantly lower MSE than the adaptive one for
                // two consecutive calculations. Reset the adaptive channel.
                WebRtcAecm_ResetAdaptiveChannel(aecm);
            } else if (((MIN_MSE_DIFF * mseStored) > (mseAdapt << MSE_RESOLUTION)) & (mseAdapt
                                                                                      < aecm->mseThreshold) &
                       (aecm->mseAdaptOld < aecm->mseThreshold)) {
                // The adaptive channel has a significantly lower MSE than the stored one.
                // The MSE for the adaptive channel has also been low for two consecutive
                // calculations. Store the adaptive channel.
                WebRtcAecm_StoreAdaptiveChannel(aecm, far_spectrum, echoEst);

                // Update threshold
                if (aecm->mseThreshold == (int32_t) 0x7fffffff) {
                    aecm->mseThreshold = (mseAdapt + aecm->mseAdaptOld);
                } else {
                    int scaled_threshold = aecm->mseThreshold * 5 / 8;
                    aecm->mseThreshold +=
                            ((mseAdapt - scaled_threshold) * 205) >> 8;
                }

            }

            // Reset counter
            aecm->mseChannelCount = 0;

            // Store the MSE values.
            aecm->mseStoredOld = mseStored;
            aecm->mseAdaptOld = mseAdapt;
        }
    }
    // END: Determine if we should store or reset channel estimate.
}

// CalcSuppressionGain(...)
//
// This function calculates the suppression gain that is used in the Wiener filter.
//
//
// @param  aecm     [i/n]   Handle of the AECM instance.
// @param  supGain  [out]   (Return value) Suppression gain with which to scale the noise
//                          level (Q14).
//
//
int16_t WebRtcAecm_CalcSuppressionGain(AecmCore *const aecm) {
    int32_t tmp32no1;

    int16_t supGain = 0;
    int16_t tmp16no1;
    int16_t dE = 0;

    // Determine suppression gain used in the Wiener filter. The gain is based on a mix of far
    // end energy and echo estimation error.
    // Adjust for the far end signal level. A low signal level indicates no far end signal,
    // hence we set the suppression gain to 0
    if (!aecm->currentVADValue) {
        supGain = 0;
    } else {
        // Adjust for possible double talk. If we have large variations in estimation error we
        // likely have double talk (or poor channel).
        tmp16no1 = (aecm->nearLogEnergy[0] - aecm->echoStoredLogEnergy[0] - ENERGY_DEV_OFFSET);
        dE = WEBRTC_SPL_ABS_W16(tmp16no1);

        if (dE < ENERGY_DEV_TOL) {
            // Likely no double talk. The better estimation, the more we can suppress signal.
            // Update counters
            if (dE < SUPGAIN_EPC_DT) {
                tmp32no1 = aecm->supGainErrParamDiffAB * dE;
                tmp32no1 += (SUPGAIN_EPC_DT >> 1);
                tmp16no1 = (int16_t) WebRtcSpl_DivW32W16(tmp32no1, SUPGAIN_EPC_DT);
                supGain = aecm->supGainErrParamA - tmp16no1;
            } else {
                tmp32no1 = aecm->supGainErrParamDiffBD * (ENERGY_DEV_TOL - dE);
                tmp32no1 += ((ENERGY_DEV_TOL - SUPGAIN_EPC_DT) >> 1);
                tmp16no1 = (int16_t) WebRtcSpl_DivW32W16(tmp32no1, (ENERGY_DEV_TOL
                                                                    - SUPGAIN_EPC_DT));
                supGain = aecm->supGainErrParamD + tmp16no1;
            }
        } else {
            // Likely in double talk. Use default value
            supGain = aecm->supGainErrParamD;
        }
    }

    if (supGain > aecm->supGainOld) {
        tmp16no1 = supGain;
    } else {
        tmp16no1 = aecm->supGainOld;
    }
    aecm->supGainOld = supGain;
    aecm->supGain += (int16_t) ((tmp16no1 - aecm->supGain) >> 4);
    /*
    if (tmp16no1 < aecm->supGain) {
        aecm->supGain += (int16_t) ((tmp16no1 - aecm->supGain) >> 4);
    } else {
        aecm->supGain += (int16_t) ((tmp16no1 - aecm->supGain) >> 4);
    }*/

    // END: Update suppression gain

    return aecm->supGain;
}

void WebRtcAecm_BufferFarFrame(AecmCore *const aecm,
                               const int16_t *const farend,
                               const int farLen) {
    int writeLen = farLen, writePos = 0;

    // Check if the write position must be wrapped
    while (aecm->farBufWritePos + writeLen > FAR_BUF_LEN) {
        // Write to remaining buffer space before wrapping
        writeLen = FAR_BUF_LEN - aecm->farBufWritePos;
        memcpy(aecm->farBuf + aecm->farBufWritePos, farend + writePos,
               sizeof(int16_t) * writeLen);
        aecm->farBufWritePos = 0;
        writePos = writeLen;
        writeLen = farLen - writeLen;
    }

    memcpy(aecm->farBuf + aecm->farBufWritePos, farend + writePos,
           sizeof(int16_t) * writeLen);
    aecm->farBufWritePos += writeLen;
}

void WebRtcAecm_FetchFarFrame(AecmCore *const aecm,
                              int16_t *const farend,
                              const int farLen,
                              const int knownDelay) {
    int readLen = farLen;
    int readPos = 0;
    int delayChange = knownDelay - aecm->lastKnownDelay;

    aecm->farBufReadPos -= delayChange;

    // Check if delay forces a read position wrap
    while (aecm->farBufReadPos < 0) {
        aecm->farBufReadPos += FAR_BUF_LEN;
    }
    while (aecm->farBufReadPos > FAR_BUF_LEN - 1) {
        aecm->farBufReadPos -= FAR_BUF_LEN;
    }

    aecm->lastKnownDelay = knownDelay;

    // Check if read position must be wrapped
    while (aecm->farBufReadPos + readLen > FAR_BUF_LEN) {

        // Read from remaining buffer space before wrapping
        readLen = FAR_BUF_LEN - aecm->farBufReadPos;
        memcpy(farend + readPos, aecm->farBuf + aecm->farBufReadPos,
               sizeof(int16_t) * readLen);
        aecm->farBufReadPos = 0;
        readPos = readLen;
        readLen = farLen - readLen;
    }
    memcpy(farend + readPos, aecm->farBuf + aecm->farBufReadPos,
           sizeof(int16_t) * readLen);
    aecm->farBufReadPos += readLen;
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

uint32_t WebRtcSpl_DivU32U16(uint32_t num, uint16_t den) {
    // Guard against division with 0
    if (den != 0) {
        return (uint32_t) (num / den);
    } else {
        return (uint32_t) 0xFFFFFFFF;
    }
}

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

int16_t WebRtcSpl_MaxAbsValueW16(const int16_t *vector, size_t length) {
    size_t i = 0;
    int absolute = 0, maximum = 0;

    for (i = 0; i < length; i++) {
        absolute = abs((int) vector[i]);

        if (absolute > maximum) {
            maximum = absolute;
        }
    }

    // Guard the case for abs(-32768).
    if (maximum > 32767) {
        maximum = 32767;
    }

    return (int16_t) maximum;
}


// Square root of Hanning window in Q14.
static const ALIGN8_BEG int16_t WebRtcAecm_kSqrtHanning[] ALIGN8_END = {
        0, 399, 798, 1196, 1594, 1990, 2386, 2780, 3172,
        3562, 3951, 4337, 4720, 5101, 5478, 5853, 6224,
        6591, 6954, 7313, 7668, 8019, 8364, 8705, 9040,
        9370, 9695, 10013, 10326, 10633, 10933, 11227, 11514,
        11795, 12068, 12335, 12594, 12845, 13089, 13325, 13553,
        13773, 13985, 14189, 14384, 14571, 14749, 14918, 15079,
        15231, 15373, 15506, 15631, 15746, 15851, 15947, 16034,
        16111, 16179, 16237, 16286, 16325, 16354, 16373, 16384
};

#ifdef AECM_WITH_ABS_APPROX
//Q15 alpha = 0.99439986968132  const Factor for magnitude approximation
static const uint16_t kAlpha1 = 32584;
//Q15 beta = 0.12967166976970   const Factor for magnitude approximation
static const uint16_t kBeta1 = 4249;
//Q15 alpha = 0.94234827210087  const Factor for magnitude approximation
static const uint16_t kAlpha2 = 30879;
//Q15 beta = 0.33787806009150   const Factor for magnitude approximation
static const uint16_t kBeta2 = 11072;
//Q15 alpha = 0.82247698684306  const Factor for magnitude approximation
static const uint16_t kAlpha3 = 26951;
//Q15 beta = 0.57762063060713   const Factor for magnitude approximation
static const uint16_t kBeta3 = 18927;
#endif

static const int16_t kNoiseEstQDomain = 15;
static const int16_t kNoiseEstIncCount = 5;

static void ComfortNoise(AecmCore *aecm,
                         const uint16_t *dfa,
                         ComplexInt16 *out,
                         const int16_t *lambda);

static void WindowAndFFT(AecmCore *aecm,
                         int16_t *fft,
                         const int16_t *time_signal,
                         ComplexInt16 *freq_signal,
                         int time_signal_scaling) {
    int i = 0;

    // FFT of signal
    for (i = 0; i < PART_LEN; i++) {
        // Window time domain signal and insert into real part of
        // transformation array |fft|
        int16_t scaled_time_signal = time_signal[i] * (1 << time_signal_scaling);
        fft[i] = (int16_t) ((scaled_time_signal * WebRtcAecm_kSqrtHanning[i]) >> 14);
        scaled_time_signal = time_signal[i + PART_LEN] * (1 << time_signal_scaling);
        fft[PART_LEN + i] = (int16_t) ((
                scaled_time_signal * WebRtcAecm_kSqrtHanning[PART_LEN - i]) >> 14);
    }

    // Do forward FFT, then take only the first PART_LEN complex samples,
    // and change signs of the imaginary parts.
    WebRtcSpl_RealForwardFFT(aecm->real_fft, fft, (int16_t *) freq_signal);
    for (i = 0; i < PART_LEN; i++) {
        freq_signal[i].imag = -freq_signal[i].imag;
    }
}

static void InverseFFTAndWindow(AecmCore *aecm,
                                int16_t *fft,
                                ComplexInt16 *efw,
                                int16_t *output,
                                const int16_t *nearendClean) {
    int i, j, outCFFT;
    int32_t tmp32no1;
    // Reuse |efw| for the inverse FFT output after transferring
    // the contents to |fft|.
    int16_t *ifft_out = (int16_t *) efw;

    // Synthesis
    for (i = 1, j = 2; i < PART_LEN; i += 1, j += 2) {
        fft[j] = efw[i].real;
        fft[j + 1] = -efw[i].imag;
    }
    fft[0] = efw[0].real;
    fft[1] = -efw[0].imag;

    fft[PART_LEN2] = efw[PART_LEN].real;
    fft[PART_LEN2 + 1] = -efw[PART_LEN].imag;

    // Inverse FFT. Keep outCFFT to scale the samples in the next block.
    outCFFT = WebRtcSpl_RealInverseFFT(aecm->real_fft, fft, ifft_out);
    for (i = 0; i < PART_LEN; i++) {
        ifft_out[i] = (int16_t) WEBRTC_SPL_MUL_16_16_RSFT_WITH_ROUND(
                ifft_out[i], WebRtcAecm_kSqrtHanning[i], 14);
        tmp32no1 = WEBRTC_SPL_SHIFT_W32((int32_t) ifft_out[i],
                                        outCFFT - aecm->dfaCleanQDomain);
        output[i] = (int16_t) WEBRTC_SPL_SAT(32767,
                                             tmp32no1 + aecm->outBuf[i],
                                             -32768);

        tmp32no1 = (ifft_out[PART_LEN + i] *
                    WebRtcAecm_kSqrtHanning[PART_LEN - i]) >> 14;
        tmp32no1 = WEBRTC_SPL_SHIFT_W32(tmp32no1,
                                        outCFFT - aecm->dfaCleanQDomain);
        aecm->outBuf[i] = (int16_t) WEBRTC_SPL_SAT(32767,
                                                   tmp32no1,
                                                   -32768);
    }

    // Copy the current block to the old position
    // (aecm->outBuf is shifted elsewhere)
    memcpy(aecm->xBuf, aecm->xBuf + PART_LEN, sizeof(int16_t) * PART_LEN);
    memcpy(aecm->dBufNoisy,
           aecm->dBufNoisy + PART_LEN,
           sizeof(int16_t) * PART_LEN);
    if (nearendClean != NULL) {
        memcpy(aecm->dBufClean,
               aecm->dBufClean + PART_LEN,
               sizeof(int16_t) * PART_LEN);
    }
}

// Transforms a time domain signal into the frequency domain, outputting the
// complex valued signal, absolute value and sum of absolute values.
//
// time_signal          [in]    Pointer to time domain signal
// freq_signal_real     [out]   Pointer to real part of frequency domain array
// freq_signal_imag     [out]   Pointer to imaginary part of frequency domain
//                              array
// freq_signal_abs      [out]   Pointer to absolute value of frequency domain
//                              array
// freq_signal_sum_abs  [out]   Pointer to the sum of all absolute values in
//                              the frequency domain array
// return value                 The Q-domain of current frequency values
//
static int TimeToFrequencyDomain(AecmCore *aecm,
                                 const int16_t *time_signal,
                                 ComplexInt16 *freq_signal,
                                 uint16_t *freq_signal_abs,
                                 uint32_t *freq_signal_sum_abs) {
    int i = 0;
    int time_signal_scaling = 0;

    int32_t tmp32no1 = 0;
    int32_t tmp32no2 = 0;

    // In fft_buf, +16 for 32-byte alignment.
    int16_t fft_buf[PART_LEN4 + 16];
    int16_t *fft = (int16_t *) (((uintptr_t) fft_buf + 31) & ~31);

    int16_t tmp16no1;
#ifndef WEBRTC_ARCH_ARM_V7
    int16_t tmp16no2;
#endif
#ifdef AECM_WITH_ABS_APPROX
    int16_t max_value = 0;
    int16_t min_value = 0;
    uint16_t alpha = 0;
    uint16_t beta = 0;
#endif

#ifdef AECM_DYNAMIC_Q
    tmp16no1 = WebRtcSpl_MaxAbsValueW16(time_signal, PART_LEN2);
    time_signal_scaling = WebRtcSpl_NormW16(tmp16no1);
#endif

    WindowAndFFT(aecm, fft, time_signal, freq_signal, time_signal_scaling);

    // Extract imaginary and real part, calculate the magnitude for
    // all frequency bins
    freq_signal[0].imag = 0;
    freq_signal[PART_LEN].imag = 0;
    freq_signal_abs[0] = (uint16_t) WEBRTC_SPL_ABS_W16(freq_signal[0].real);
    freq_signal_abs[PART_LEN] = (uint16_t) WEBRTC_SPL_ABS_W16(
            freq_signal[PART_LEN].real);
    (*freq_signal_sum_abs) = (uint32_t) (freq_signal_abs[0]) +
                             (uint32_t) (freq_signal_abs[PART_LEN]);

    for (i = 1; i < PART_LEN; i++) {
        if (freq_signal[i].real == 0) {
            freq_signal_abs[i] = (uint16_t) WEBRTC_SPL_ABS_W16(freq_signal[i].imag);
        } else if (freq_signal[i].imag == 0) {
            freq_signal_abs[i] = (uint16_t) WEBRTC_SPL_ABS_W16(freq_signal[i].real);
        } else {
            // Approximation for magnitude of complex fft output
            // magn = sqrt(real^2 + imag^2)
            // magn ~= alpha * max(|imag|,|real|) + beta * min(|imag|,|real|)
            //
            // The parameters alpha and beta are stored in Q15

#ifdef AECM_WITH_ABS_APPROX
            tmp16no1 = WEBRTC_SPL_ABS_W16(freq_signal[i].real);
            tmp16no2 = WEBRTC_SPL_ABS_W16(freq_signal[i].imag);

            if(tmp16no1 > tmp16no2)
            {
              max_value = tmp16no1;
              min_value = tmp16no2;
            } else
            {
              max_value = tmp16no2;
              min_value = tmp16no1;
            }

            // Magnitude in Q(-6)
            if ((max_value >> 2) > min_value)
            {
              alpha = kAlpha1;
              beta = kBeta1;
            } else if ((max_value >> 1) > min_value)
            {
              alpha = kAlpha2;
              beta = kBeta2;
            } else
            {
              alpha = kAlpha3;
              beta = kBeta3;
            }
            tmp16no1 = (int16_t)((max_value * alpha) >> 15);
            tmp16no2 = (int16_t)((min_value * beta) >> 15);
            freq_signal_abs[i] = (uint16_t)tmp16no1 + (uint16_t)tmp16no2;
#else
#ifdef WEBRTC_ARCH_ARM_V7
            __asm __volatile(
              "smulbb %[tmp32no1], %[real], %[real]\n\t"
              "smlabb %[tmp32no2], %[imag], %[imag], %[tmp32no1]\n\t"
              :[tmp32no1]"+&r"(tmp32no1),
               [tmp32no2]"=r"(tmp32no2)
              :[real]"r"(freq_signal[i].real),
               [imag]"r"(freq_signal[i].imag)
            );
#else
            tmp16no1 = WEBRTC_SPL_ABS_W16(freq_signal[i].real);
            tmp16no2 = WEBRTC_SPL_ABS_W16(freq_signal[i].imag);
            tmp32no1 = tmp16no1 * tmp16no1;
            tmp32no2 = tmp16no2 * tmp16no2;
            tmp32no2 = WebRtcSpl_AddSatW32(tmp32no1, tmp32no2);
#endif // WEBRTC_ARCH_ARM_V7
            tmp32no1 = WebRtcSpl_SqrtFloor(tmp32no2);

            freq_signal_abs[i] = (uint16_t) tmp32no1;
#endif // AECM_WITH_ABS_APPROX
        }
        (*freq_signal_sum_abs) += (uint32_t) freq_signal_abs[i];
    }

    return time_signal_scaling;
}

int
WebRtcAecm_ProcessBlock(AecmCore *aecm,
                        const int16_t *farend,
                        const int16_t *nearendNoisy,
                        const int16_t *nearendClean,
                        int16_t *output) {
    int i;

    uint32_t xfaSum;
    uint32_t dfaNoisySum;
    uint32_t dfaCleanSum;
    uint32_t echoEst32Gained;
    uint32_t tmpU32;

    int32_t tmp32no1;

    uint16_t xfa[PART_LEN1];
    uint16_t dfaNoisy[PART_LEN1];
    uint16_t dfaClean[PART_LEN1];
    uint16_t *ptrDfaClean = dfaClean;
    const uint16_t *far_spectrum_ptr = NULL;

    // 32 byte aligned buffers (with +8 or +16).
    // TODO(kma): define fft with ComplexInt16.
    int16_t fft_buf[PART_LEN4 + 2 + 16]; // +2 to make a loop safe.
    int32_t echoEst32_buf[PART_LEN1 + 8];
    int32_t dfw_buf[PART_LEN2 + 8];
    int32_t efw_buf[PART_LEN2 + 8];

    int16_t *fft = (int16_t *) (((uintptr_t) fft_buf + 31) & ~31);
    int32_t *echoEst32 = (int32_t *) (((uintptr_t) echoEst32_buf + 31) & ~31);
    ComplexInt16 *dfw = (ComplexInt16 *) (((uintptr_t) dfw_buf + 31) & ~31);
    ComplexInt16 *efw = (ComplexInt16 *) (((uintptr_t) efw_buf + 31) & ~31);

    int16_t hnl[PART_LEN1];
    int16_t numPosCoef = 0;
    int16_t nlpGain = 0;
    int delay;
    int16_t tmp16no1;
    int16_t tmp16no2;
    int16_t mu;
    int16_t supGain;
    int16_t zeros32, zeros16;
    int16_t zerosDBufNoisy, zerosDBufClean, zerosXBuf;
    int far_q;
    int16_t resolutionDiff, qDomainDiff, dfa_clean_q_domain_diff;

    const int kMinPrefBand = 4;
    const int kMaxPrefBand = 24;
    int32_t avgHnl32 = 0;

    // Determine startup state. There are three states:
    // (0) the first CONV_LEN blocks
    // (1) another CONV_LEN blocks
    // (2) the rest

    if (aecm->startupState < 2) {
        aecm->startupState = (aecm->totCount >= CONV_LEN) +
                             (aecm->totCount >= CONV_LEN2);
    }
    // END: Determine startup state

    // Buffer near and far end signals
    memcpy(aecm->xBuf + PART_LEN, farend, sizeof(int16_t) * PART_LEN);
    memcpy(aecm->dBufNoisy + PART_LEN, nearendNoisy, sizeof(int16_t) * PART_LEN);
    if (nearendClean != NULL) {
        memcpy(aecm->dBufClean + PART_LEN,
               nearendClean,
               sizeof(int16_t) * PART_LEN);
    }

    // Transform far end signal from time domain to frequency domain.
    far_q = TimeToFrequencyDomain(aecm,
                                  aecm->xBuf,
                                  dfw,
                                  xfa,
                                  &xfaSum);

    // Transform noisy near end signal from time domain to frequency domain.
    zerosDBufNoisy = TimeToFrequencyDomain(aecm,
                                           aecm->dBufNoisy,
                                           dfw,
                                           dfaNoisy,
                                           &dfaNoisySum);
    aecm->dfaNoisyQDomainOld = aecm->dfaNoisyQDomain;
    aecm->dfaNoisyQDomain = (int16_t) zerosDBufNoisy;


    if (nearendClean == NULL) {
        ptrDfaClean = dfaNoisy;
        aecm->dfaCleanQDomainOld = aecm->dfaNoisyQDomainOld;
        aecm->dfaCleanQDomain = aecm->dfaNoisyQDomain;
        //dfaCleanSum = dfaNoisySum;
    } else {
        // Transform clean near end signal from time domain to frequency domain.
        zerosDBufClean = TimeToFrequencyDomain(aecm,
                                               aecm->dBufClean,
                                               dfw,
                                               dfaClean,
                                               &dfaCleanSum);
        aecm->dfaCleanQDomainOld = aecm->dfaCleanQDomain;
        aecm->dfaCleanQDomain = (int16_t) zerosDBufClean;
    }

    // Get the delay
    // Save far-end history and estimate delay
    WebRtcAecm_UpdateFarHistory(aecm, xfa, far_q);
    if (WebRtc_AddFarSpectrumFix(aecm->delay_estimator_farend,
                                 xfa,
                                 PART_LEN1,
                                 far_q) == -1) {
        return -1;
    }
    delay = WebRtc_DelayEstimatorProcessFix(aecm->delay_estimator,
                                            dfaNoisy,
                                            PART_LEN1,
                                            zerosDBufNoisy);
    if (delay == -1) {
        return -1;
    } else if (delay == -2) {
        // If the delay is unknown, we assume zero.
        // NOTE: this will have to be adjusted if we ever add lookahead.
        delay = 0;
    }

    if (aecm->fixedDelay >= 0) {
        // Use fixed delay
        delay = aecm->fixedDelay;
    }

    // Get aligned far end spectrum
    far_spectrum_ptr = WebRtcAecm_AlignedFarend(aecm, &far_q, delay);
    zerosXBuf = (int16_t) far_q;
    if (far_spectrum_ptr == NULL) {
        return -1;
    }

    // Calculate log(energy) and update energy threshold levels
    WebRtcAecm_CalcEnergies(aecm,
                            far_spectrum_ptr,
                            zerosXBuf,
                            dfaNoisySum,
                            echoEst32);

    // Calculate stepsize
    mu = WebRtcAecm_CalcStepSize(aecm);

    // Update counters
    aecm->totCount++;

    // This is the channel estimation algorithm.
    // It is base on NLMS but has a variable step length,
    // which was calculated above.
    WebRtcAecm_UpdateChannel(aecm,
                             far_spectrum_ptr,
                             zerosXBuf,
                             dfaNoisy,
                             mu,
                             echoEst32);
    supGain = WebRtcAecm_CalcSuppressionGain(aecm);


    // Calculate Wiener filter hnl[]
    for (i = 0; i < PART_LEN1; i++) {
        // Far end signal through channel estimate in Q8
        // How much can we shift right to preserve resolution
        tmp32no1 = echoEst32[i] - aecm->echoFilt[i];
        aecm->echoFilt[i] += (int32_t) (((int64_t) (tmp32no1) * 50) >> 8);

        zeros32 = WebRtcSpl_NormW32(aecm->echoFilt[i]) + 1;
        zeros16 = WebRtcSpl_NormW16(supGain) + 1;
        if (zeros32 + zeros16 > 16) {
            // Multiplication is safe
            // Result in
            // Q(RESOLUTION_CHANNEL+RESOLUTION_SUPGAIN+
            //   aecm->xfaQDomainBuf[diff])
            echoEst32Gained = WEBRTC_SPL_UMUL_32_16((uint32_t) aecm->echoFilt[i],
                                                    (uint16_t) supGain);
            resolutionDiff = 14 - RESOLUTION_CHANNEL16 - RESOLUTION_SUPGAIN;
            resolutionDiff += (aecm->dfaCleanQDomain - zerosXBuf);
        } else {
            tmp16no1 = 17 - zeros32 - zeros16;
            resolutionDiff = 14 + tmp16no1 - RESOLUTION_CHANNEL16 -
                             RESOLUTION_SUPGAIN;
            resolutionDiff += (aecm->dfaCleanQDomain - zerosXBuf);
            if (zeros32 > tmp16no1) {
                echoEst32Gained = WEBRTC_SPL_UMUL_32_16((uint32_t) aecm->echoFilt[i],
                                                        supGain >> tmp16no1);
            } else {
                // Result in Q-(RESOLUTION_CHANNEL+RESOLUTION_SUPGAIN-16)
                echoEst32Gained = (aecm->echoFilt[i] >> tmp16no1) * supGain;
            }
        }

        zeros16 = WebRtcSpl_NormW16(aecm->nearFilt[i]);
        assert(zeros16 >= 0);  // |zeros16| is a norm, hence non-negative.
        dfa_clean_q_domain_diff = aecm->dfaCleanQDomain - aecm->dfaCleanQDomainOld;
        if (zeros16 < dfa_clean_q_domain_diff && aecm->nearFilt[i]) {
            tmp16no1 = aecm->nearFilt[i] * (1 << zeros16);
            qDomainDiff = zeros16 - dfa_clean_q_domain_diff;
            tmp16no2 = ptrDfaClean[i] >> -qDomainDiff;
        } else {
            tmp16no1 = dfa_clean_q_domain_diff < 0
                       ? aecm->nearFilt[i] >> -dfa_clean_q_domain_diff
                       : aecm->nearFilt[i] * (1 << dfa_clean_q_domain_diff);
            qDomainDiff = 0;
            tmp16no2 = ptrDfaClean[i];
        }
        tmp32no1 = (int32_t) (tmp16no2 - tmp16no1);
        tmp16no2 = (int16_t) (tmp32no1 >> 4);
        tmp16no2 += tmp16no1;
        zeros16 = WebRtcSpl_NormW16(tmp16no2);
        if ((tmp16no2) & (-qDomainDiff > zeros16)) {
            aecm->nearFilt[i] = 32767;
        } else {
            aecm->nearFilt[i] = qDomainDiff < 0 ? tmp16no2 * (1 << -qDomainDiff)
                                                : tmp16no2 >> qDomainDiff;
        }

        // Wiener filter coefficients, resulting hnl in Q14
        if (echoEst32Gained == 0) {
            hnl[i] = ONE_Q14;
        } else if (aecm->nearFilt[i] == 0) {
            hnl[i] = 0;
        } else {
            // Multiply the suppression gain
            // Rounding
            echoEst32Gained += (uint32_t) (aecm->nearFilt[i] >> 1);
            tmpU32 = WebRtcSpl_DivU32U16(echoEst32Gained,
                                         (uint16_t) aecm->nearFilt[i]);

            // Current resolution is
            // Q-(RESOLUTION_CHANNEL+RESOLUTION_SUPGAIN- max(0,17-zeros16- zeros32))
            // Make sure we are in Q14
            tmp32no1 = (int32_t) WEBRTC_SPL_SHIFT_W32(tmpU32, resolutionDiff);
            if (tmp32no1 > ONE_Q14) {
                hnl[i] = 0;
            } else if (tmp32no1 < 0) {
                hnl[i] = ONE_Q14;
            } else {
                // 1-echoEst/dfa
                hnl[i] = ONE_Q14 - (int16_t) tmp32no1;
                if (hnl[i] < 0) {
                    hnl[i] = 0;
                }
            }
        }
        if (hnl[i]) {
            numPosCoef++;
        }
    }
    // Only in wideband. Prevent the gain in upper band from being larger than
    // in lower band.
    if (aecm->mult == 2) {
        // TODO(bjornv): Investigate if the scaling of hnl[i] below can cause
        //               speech distortion in double-talk.
        for (i = 0; i < PART_LEN1; i++) {
            hnl[i] = (int16_t) ((hnl[i] * hnl[i]) >> 14);
        }

        for (i = kMinPrefBand; i <= kMaxPrefBand; i++) {
            avgHnl32 += (int32_t) hnl[i];
        }
        assert(kMaxPrefBand - kMinPrefBand + 1 > 0);
        avgHnl32 /= (kMaxPrefBand - kMinPrefBand + 1);

        for (i = kMaxPrefBand; i < PART_LEN1; i++) {
            if (hnl[i] > (int16_t) avgHnl32) {
                hnl[i] = (int16_t) avgHnl32;
            }
        }
    }

    // Calculate NLP gain, result is in Q14
    if (aecm->nlpFlag) {
        for (i = 0; i < PART_LEN1; i++) {
            // Truncate values close to zero and one.
            if (hnl[i] > NLP_COMP_HIGH) {
                hnl[i] = ONE_Q14;
            } else if (hnl[i] < NLP_COMP_LOW) {
                hnl[i] = 0;
            }

            // Remove outliers
            if (numPosCoef < 3) {
                nlpGain = 0;
            } else {
                nlpGain = ONE_Q14;
            }

            // NLP
            if ((hnl[i] == ONE_Q14) && (nlpGain == ONE_Q14)) {
                hnl[i] = ONE_Q14;
            } else {
                hnl[i] = (int16_t) ((hnl[i] * nlpGain) >> 14);
            }

            // multiply with Wiener coefficients
            efw[i].real = (int16_t) (WEBRTC_SPL_MUL_16_16_RSFT_WITH_ROUND(dfw[i].real,
                                                                          hnl[i], 14));
            efw[i].imag = (int16_t) (WEBRTC_SPL_MUL_16_16_RSFT_WITH_ROUND(dfw[i].imag,
                                                                          hnl[i], 14));
        }
    } else {
        // multiply with Wiener coefficients
        for (i = 0; i < PART_LEN1; i++) {
            efw[i].real = (int16_t) (WEBRTC_SPL_MUL_16_16_RSFT_WITH_ROUND(dfw[i].real,
                                                                          hnl[i], 14));
            efw[i].imag = (int16_t) (WEBRTC_SPL_MUL_16_16_RSFT_WITH_ROUND(dfw[i].imag,
                                                                          hnl[i], 14));
        }
    }

    if (aecm->cngMode == AecmTrue) {
        ComfortNoise(aecm, ptrDfaClean, efw, hnl);
    }

    InverseFFTAndWindow(aecm, fft, efw, output, nearendClean);

    return 0;
}

static void ComfortNoise(AecmCore *aecm,
                         const uint16_t *dfa,
                         ComplexInt16 *out,
                         const int16_t *lambda) {
    int16_t i;
    int16_t tmp16;
    int32_t tmp32;

    int16_t randW16[PART_LEN];
    int16_t uReal[PART_LEN1];
    int16_t uImag[PART_LEN1];
    int32_t outLShift32;
    int16_t noiseRShift16[PART_LEN1];

    int16_t shiftFromNearToNoise = kNoiseEstQDomain - aecm->dfaCleanQDomain;
    int16_t minTrackShift;

    assert(shiftFromNearToNoise >= 0);
    assert(shiftFromNearToNoise < 16);

    if (aecm->noiseEstCtr < 100) {
        // Track the minimum more quickly initially.
        aecm->noiseEstCtr++;
        minTrackShift = 6;
    } else {
        minTrackShift = 9;
    }

    // Estimate noise power.
    for (i = 0; i < PART_LEN1; i++) {
        // Shift to the noise domain.
        tmp32 = (int32_t) dfa[i];
        outLShift32 = tmp32 << shiftFromNearToNoise;

        if (outLShift32 < aecm->noiseEst[i]) {
            // Reset "too low" counter
            aecm->noiseEstTooLowCtr[i] = 0;
            // Track the minimum.
            if (aecm->noiseEst[i] < (1 << minTrackShift)) {
                // For small values, decrease noiseEst[i] every
                // |kNoiseEstIncCount| block. The regular approach below can not
                // go further down due to truncation.
                aecm->noiseEstTooHighCtr[i]++;
                if (aecm->noiseEstTooHighCtr[i] >= kNoiseEstIncCount) {
                    aecm->noiseEst[i]--;
                    aecm->noiseEstTooHighCtr[i] = 0; // Reset the counter
                }
            } else {
                aecm->noiseEst[i] -= ((aecm->noiseEst[i] - outLShift32)
                        >> minTrackShift);
            }
        } else {
            // Reset "too high" counter
            aecm->noiseEstTooHighCtr[i] = 0;
            // Ramp slowly upwards until we hit the minimum again.
            if ((aecm->noiseEst[i] >> 19) > 0) {
                // Avoid overflow.
                // Multiplication with 2049 will cause wrap around. Scale
                // down first and then multiply
                aecm->noiseEst[i] >>= 11;
                aecm->noiseEst[i] *= 2049;
            } else if ((aecm->noiseEst[i] >> 11) > 0) {
                // Large enough for relative increase
                aecm->noiseEst[i] *= 2049;
                aecm->noiseEst[i] >>= 11;
            } else {
                // Make incremental increases based on size every
                // |kNoiseEstIncCount| block
                aecm->noiseEstTooLowCtr[i]++;
                if (aecm->noiseEstTooLowCtr[i] >= kNoiseEstIncCount) {
                    aecm->noiseEst[i] += (aecm->noiseEst[i] >> 9) + 1;
                    aecm->noiseEstTooLowCtr[i] = 0; // Reset counter
                }
            }
        }
    }

    for (i = 0; i < PART_LEN1; i++) {
        tmp32 = aecm->noiseEst[i] >> shiftFromNearToNoise;
        if (tmp32 > 32767) {
            tmp32 = 32767;
            aecm->noiseEst[i] = tmp32 << shiftFromNearToNoise;
        }
        noiseRShift16[i] = (int16_t) tmp32;

        tmp16 = ONE_Q14 - lambda[i];
        noiseRShift16[i] = (int16_t) ((tmp16 * noiseRShift16[i]) >> 14);
    }

    // Generate a uniform random array on [0 2^15-1].
    WebRtcSpl_RandUArray(randW16, PART_LEN, &aecm->seed);

    // Generate noise according to estimated energy.
    uReal[0] = 0; // Reject LF noise.
    uImag[0] = 0;
    for (i = 1; i < PART_LEN1; i++) {
        // Get a random index for the cos and sin tables over [0 359].
        tmp16 = (int16_t) ((359 * randW16[i - 1]) >> 15);

        // Tables are in Q13.
        uReal[i] = (int16_t) ((noiseRShift16[i] * WebRtcAecm_kCosTable[tmp16]) >>
                                                                               13);
        uImag[i] = (int16_t) ((-noiseRShift16[i] * WebRtcAecm_kSinTable[tmp16]) >>
                                                                                13);
    }
    uImag[PART_LEN] = 0;

    for (i = 0; i < PART_LEN1; i++) {
        out[i].real = WebRtcSpl_AddSatW16(out[i].real, uReal[i]);
        out[i].imag = WebRtcSpl_AddSatW16(out[i].imag, uImag[i]);
    }
}

#include <stddef.h>  // size_t
#include <stdlib.h>
#include <string.h>

// Get address of region(s) from which we can read data.
// If the region is contiguous, |data_ptr_bytes_2| will be zero.
// If non-contiguous, |data_ptr_bytes_2| will be the size in bytes of the second
// region. Returns room available to be read or |element_count|, whichever is
// smaller.
static size_t GetBufferReadRegions(RingBuffer *buf,
                                   size_t element_count,
                                   void **data_ptr_1,
                                   size_t *data_ptr_bytes_1,
                                   void **data_ptr_2,
                                   size_t *data_ptr_bytes_2) {

    const size_t readable_elements = WebRtc_available_read(buf);
    const size_t read_elements = (readable_elements < element_count ?
                                  readable_elements : element_count);
    const size_t margin = buf->element_count - buf->read_pos;

    // Check to see if read is not contiguous.
    if (read_elements > margin) {
        // Write data in two blocks that wrap the buffer.
        *data_ptr_1 = buf->data + buf->read_pos * buf->element_size;
        *data_ptr_bytes_1 = margin * buf->element_size;
        *data_ptr_2 = buf->data;
        *data_ptr_bytes_2 = (read_elements - margin) * buf->element_size;
    } else {
        *data_ptr_1 = buf->data + buf->read_pos * buf->element_size;
        *data_ptr_bytes_1 = read_elements * buf->element_size;
        *data_ptr_2 = NULL;
        *data_ptr_bytes_2 = 0;
    }

    return read_elements;
}

RingBuffer *WebRtc_CreateBuffer(size_t element_count, size_t element_size) {
    RingBuffer *self = NULL;
    if (element_count == 0 || element_size == 0) {
        return NULL;
    }

    self = malloc(sizeof(RingBuffer));
    if (!self) {
        return NULL;
    }

    self->data = malloc(element_count * element_size);
    if (!self->data) {
        free(self);
        return NULL;
    }

    self->element_count = element_count;
    self->element_size = element_size;
    WebRtc_InitBuffer(self);

    return self;
}

void WebRtc_InitBuffer(RingBuffer *self) {
    self->read_pos = 0;
    self->write_pos = 0;
    self->rw_wrap = SAME_WRAP;

    // Initialize buffer to zeros
    memset(self->data, 0, self->element_count * self->element_size);
}

void WebRtc_FreeBuffer(void *handle) {
    RingBuffer *self = (RingBuffer *) handle;
    if (!self) {
        return;
    }

    free(self->data);
    free(self);
}

size_t WebRtc_ReadBuffer(RingBuffer *self,
                         void **data_ptr,
                         void *data,
                         size_t element_count) {

    if (self == NULL) {
        return 0;
    }
    if (data == NULL) {
        return 0;
    }

    {
        void *buf_ptr_1 = NULL;
        void *buf_ptr_2 = NULL;
        size_t buf_ptr_bytes_1 = 0;
        size_t buf_ptr_bytes_2 = 0;
        const size_t read_count = GetBufferReadRegions(self,
                                                       element_count,
                                                       &buf_ptr_1,
                                                       &buf_ptr_bytes_1,
                                                       &buf_ptr_2,
                                                       &buf_ptr_bytes_2);
        if (buf_ptr_bytes_2 > 0) {
            // We have a wrap around when reading the buffer. Copy the buffer data to
            // |data| and point to it.
            memcpy(data, buf_ptr_1, buf_ptr_bytes_1);
            memcpy(((char *) data) + buf_ptr_bytes_1, buf_ptr_2, buf_ptr_bytes_2);
            buf_ptr_1 = data;
        } else if (!data_ptr) {
            // No wrap, but a memcpy was requested.
            memcpy(data, buf_ptr_1, buf_ptr_bytes_1);
        }
        if (data_ptr) {
            // |buf_ptr_1| == |data| in the case of a wrap.
            *data_ptr = read_count == 0 ? NULL : buf_ptr_1;
        }

        // Update read position
        WebRtc_MoveReadPtr(self, (int) read_count);

        return read_count;
    }
}

size_t WebRtc_WriteBuffer(RingBuffer *self,
                          const void *data,
                          size_t element_count) {
    if (!self) {
        return 0;
    }
    if (!data) {
        return 0;
    }

    {
        const size_t free_elements = WebRtc_available_write(self);
        const size_t write_elements = (free_elements < element_count ? free_elements
                                                                     : element_count);
        size_t n = write_elements;
        const size_t margin = self->element_count - self->write_pos;

        if (write_elements > margin) {
            // Buffer wrap around when writing.
            memcpy(self->data + self->write_pos * self->element_size,
                   data, margin * self->element_size);
            self->write_pos = 0;
            n -= margin;
            self->rw_wrap = DIFF_WRAP;
        }
        memcpy(self->data + self->write_pos * self->element_size,
               ((const char *) data) + ((write_elements - n) * self->element_size),
               n * self->element_size);
        self->write_pos += n;

        return write_elements;
    }
}

int WebRtc_MoveReadPtr(RingBuffer *self, int element_count) {
    if (!self) {
        return 0;
    }

    {
        // We need to be able to take care of negative changes, hence use "int"
        // instead of "size_t".
        const int free_elements = (int) WebRtc_available_write(self);
        const int readable_elements = (int) WebRtc_available_read(self);
        int read_pos = (int) self->read_pos;

        if (element_count > readable_elements) {
            element_count = readable_elements;
        }
        if (element_count < -free_elements) {
            element_count = -free_elements;
        }

        read_pos += element_count;
        if (read_pos > (int) self->element_count) {
            // Buffer wrap around. Restart read position and wrap indicator.
            read_pos -= (int) self->element_count;
            self->rw_wrap = SAME_WRAP;
        }
        if (read_pos < 0) {
            // Buffer wrap around. Restart read position and wrap indicator.
            read_pos += (int) self->element_count;
            self->rw_wrap = DIFF_WRAP;
        }

        self->read_pos = (size_t) read_pos;

        return element_count;
    }
}

size_t WebRtc_available_read(const RingBuffer *self) {
    if (!self) {
        return 0;
    }

    if (self->rw_wrap == SAME_WRAP) {
        return self->write_pos - self->read_pos;
    } else {
        return self->element_count - self->read_pos + self->write_pos;
    }
}

size_t WebRtc_available_write(const RingBuffer *self) {
    if (!self) {
        return 0;
    }

    return self->element_count - WebRtc_available_read(self);
}

typedef union {
    float float_;
    int32_t int32_;
} SpectrumType;

typedef struct {
    // Pointers to mean values of spectrum.
    SpectrumType *mean_far_spectrum;
    // |mean_far_spectrum| initialization indicator.
    int far_spectrum_initialized;

    int spectrum_size;

    // Far-end part of binary spectrum based delay estimation.
    BinaryDelayEstimatorFarend *binary_farend;
} DelayEstimatorFarend;

typedef struct {
    // Pointers to mean values of spectrum.
    SpectrumType *mean_near_spectrum;
    // |mean_near_spectrum| initialization indicator.
    int near_spectrum_initialized;

    int spectrum_size;

    // Binary spectrum based delay estimator
    BinaryDelayEstimator *binary_handle;
} DelayEstimator;


// Number of right shifts for scaling is linearly depending on number of bits in
// the far-end binary spectrum.
static const int kShiftsAtZero = 13;  // Right shifts at zero binary spectrum.
static const int kShiftsLinearSlope = 3;

static const int32_t kProbabilityOffset = 1024;  // 2 in Q9.
static const int32_t kProbabilityLowerLimit = 8704;  // 17 in Q9.
static const int32_t kProbabilityMinSpread = 2816;  // 5.5 in Q9.

// Robust validation settings
static const float kHistogramMax = 3000.f;
static const float kLastHistogramMax = 250.f;
static const float kMinHistogramThreshold = 1.5f;
static const int kMinRequiredHits = 10;
static const int kMaxHitsWhenPossiblyNonCausal = 10;
static const int kMaxHitsWhenPossiblyCausal = 1000;
static const float kQ14Scaling = 1.f / (1 << 14);  // Scaling by 2^14 to get Q0.
static const float kFractionSlope = 0.05f;
static const float kMinFractionWhenPossiblyCausal = 0.5f;
static const float kMinFractionWhenPossiblyNonCausal = 0.25f;

// Counts and returns number of bits of a 32-bit word.
static int BitCount(uint32_t u32) {
    uint32_t tmp = u32 - ((u32 >> 1) & 033333333333) -
                   ((u32 >> 2) & 011111111111);
    tmp = ((tmp + (tmp >> 3)) & 030707070707);
    tmp = (tmp + (tmp >> 6));
    tmp = (tmp + (tmp >> 12) + (tmp >> 24)) & 077;

    return ((int) tmp);
}

// Compares the |binary_vector| with all rows of the |binary_matrix| and counts
// per row the number of times they have the same value.
//
// Inputs:
//      - binary_vector     : binary "vector" stored in a long
//      - binary_matrix     : binary "matrix" stored as a vector of long
//      - matrix_size       : size of binary "matrix"
//
// Output:
//      - bit_counts        : "Vector" stored as a long, containing for each
//                            row the number of times the matrix row and the
//                            input vector have the same value
//
static void BitCountComparison(uint32_t binary_vector,
                               const uint32_t *binary_matrix,
                               int matrix_size,
                               int32_t *bit_counts) {
    int n = 0;

    // Compare |binary_vector| with all rows of the |binary_matrix|
    for (; n < matrix_size; n++) {
        bit_counts[n] = (int32_t) BitCount(binary_vector ^ binary_matrix[n]);
    }
}

// Collects necessary statistics for the HistogramBasedValidation().  This
// function has to be called prior to calling HistogramBasedValidation().  The
// statistics updated and used by the HistogramBasedValidation() are:
//  1. the number of |candidate_hits|, which states for how long we have had the
//     same |candidate_delay|
//  2. the |histogram| of candidate delays over time.  This histogram is
//     weighted with respect to a reliability measure and time-varying to cope
//     with possible delay shifts.
// For further description see commented code.
//
// Inputs:
//  - candidate_delay   : The delay to validate.
//  - valley_depth_q14  : The cost function has a valley/minimum at the
//                        |candidate_delay| location.  |valley_depth_q14| is the
//                        cost function difference between the minimum and
//                        maximum locations.  The value is in the Q14 domain.
//  - valley_level_q14  : Is the cost function value at the minimum, in Q14.
static void UpdateRobustValidationStatistics(BinaryDelayEstimator *self,
                                             int candidate_delay,
                                             int32_t valley_depth_q14,
                                             int32_t valley_level_q14) {
    const float valley_depth = valley_depth_q14 * kQ14Scaling;
    float decrease_in_last_set = valley_depth;
    const int max_hits_for_slow_change = (candidate_delay < self->last_delay) ?
                                         kMaxHitsWhenPossiblyNonCausal : kMaxHitsWhenPossiblyCausal;
    int i = 0;

    assert(self->history_size == self->farend->history_size);
    // Reset |candidate_hits| if we have a new candidate.
    if (candidate_delay != self->last_candidate_delay) {
        self->candidate_hits = 0;
        self->last_candidate_delay = candidate_delay;
    }
    self->candidate_hits++;

    // The |histogram| is updated differently across the bins.
    // 1. The |candidate_delay| histogram bin is increased with the
    //    |valley_depth|, which is a simple measure of how reliable the
    //    |candidate_delay| is.  The histogram is not increased above
    //    |kHistogramMax|.
    self->histogram[candidate_delay] += valley_depth;
    if (self->histogram[candidate_delay] > kHistogramMax) {
        self->histogram[candidate_delay] = kHistogramMax;
    }
    // 2. The histogram bins in the neighborhood of |candidate_delay| are
    //    unaffected.  The neighborhood is defined as x + {-2, -1, 0, 1}.
    // 3. The histogram bins in the neighborhood of |last_delay| are decreased
    //    with |decrease_in_last_set|.  This value equals the difference between
    //    the cost function values at the locations |candidate_delay| and
    //    |last_delay| until we reach |max_hits_for_slow_change| consecutive hits
    //    at the |candidate_delay|.  If we exceed this amount of hits the
    //    |candidate_delay| is a "potential" candidate and we start decreasing
    //    these histogram bins more rapidly with |valley_depth|.
    if (self->candidate_hits < max_hits_for_slow_change) {
        decrease_in_last_set = (self->mean_bit_counts[self->compare_delay] -
                                valley_level_q14) * kQ14Scaling;
    }
    // 4. All other bins are decreased with |valley_depth|.
    // TODO(bjornv): Investigate how to make this loop more efficient.  Split up
    // the loop?  Remove parts that doesn't add too much.
    for (i = 0; i < self->history_size; ++i) {
        int is_in_last_set = (i >= self->last_delay - 2) &&
                             (i <= self->last_delay + 1) && (i != candidate_delay);
        int is_in_candidate_set = (i >= candidate_delay - 2) &&
                                  (i <= candidate_delay + 1);
        self->histogram[i] -= decrease_in_last_set * is_in_last_set +
                              valley_depth * (!is_in_last_set && !is_in_candidate_set);
        // 5. No histogram bin can go below 0.
        if (self->histogram[i] < 0) {
            self->histogram[i] = 0;
        }
    }
}

// Validates the |candidate_delay|, estimated in WebRtc_ProcessBinarySpectrum(),
// based on a mix of counting concurring hits with a modified histogram
// of recent delay estimates.  In brief a candidate is valid (returns 1) if it
// is the most likely according to the histogram.  There are a couple of
// exceptions that are worth mentioning:
//  1. If the |candidate_delay| < |last_delay| it can be that we are in a
//     non-causal state, breaking a possible echo control algorithm.  Hence, we
//     open up for a quicker change by allowing the change even if the
//     |candidate_delay| is not the most likely one according to the histogram.
//  2. There's a minimum number of hits (kMinRequiredHits) and the histogram
//     value has to reached a minimum (kMinHistogramThreshold) to be valid.
//  3. The action is also depending on the filter length used for echo control.
//     If the delay difference is larger than what the filter can capture, we
//     also move quicker towards a change.
// For further description see commented code.
//
// Input:
//  - candidate_delay     : The delay to validate.
//
// Return value:
//  - is_histogram_valid  : 1 - The |candidate_delay| is valid.
//                          0 - Otherwise.
static int HistogramBasedValidation(const BinaryDelayEstimator *self,
                                    int candidate_delay) {
    float fraction = 1.f;
    float histogram_threshold = self->histogram[self->compare_delay];
    const int delay_difference = candidate_delay - self->last_delay;
    int is_histogram_valid = 0;

    // The histogram based validation of |candidate_delay| is done by comparing
    // the |histogram| at bin |candidate_delay| with a |histogram_threshold|.
    // This |histogram_threshold| equals a |fraction| of the |histogram| at bin
    // |last_delay|.  The |fraction| is a piecewise linear function of the
    // |delay_difference| between the |candidate_delay| and the |last_delay|
    // allowing for a quicker move if
    //  i) a potential echo control filter can not handle these large differences.
    // ii) keeping |last_delay| instead of updating to |candidate_delay| could
    //     force an echo control into a non-causal state.
    // We further require the histogram to have reached a minimum value of
    // |kMinHistogramThreshold|.  In addition, we also require the number of
    // |candidate_hits| to be more than |kMinRequiredHits| to remove spurious
    // values.

    // Calculate a comparison histogram value (|histogram_threshold|) that is
    // depending on the distance between the |candidate_delay| and |last_delay|.
    // TODO(bjornv): How much can we gain by turning the fraction calculation
    // into tables?
    if (delay_difference > self->allowed_offset) {
        fraction = 1.f - kFractionSlope * (delay_difference - self->allowed_offset);
        fraction = (fraction > kMinFractionWhenPossiblyCausal ? fraction :
                    kMinFractionWhenPossiblyCausal);
    } else if (delay_difference < 0) {
        fraction = kMinFractionWhenPossiblyNonCausal -
                   kFractionSlope * delay_difference;
        fraction = (fraction > 1.f ? 1.f : fraction);
    }
    histogram_threshold *= fraction;
    histogram_threshold = (histogram_threshold > kMinHistogramThreshold ?
                           histogram_threshold : kMinHistogramThreshold);

    is_histogram_valid =
            (self->histogram[candidate_delay] >= histogram_threshold) &&
            (self->candidate_hits > kMinRequiredHits);

    return is_histogram_valid;
}

// Performs a robust validation of the |candidate_delay| estimated in
// WebRtc_ProcessBinarySpectrum().  The algorithm takes the
// |is_instantaneous_valid| and the |is_histogram_valid| and combines them
// into a robust validation.  The HistogramBasedValidation() has to be called
// prior to this call.
// For further description on how the combination is done, see commented code.
//
// Inputs:
//  - candidate_delay         : The delay to validate.
//  - is_instantaneous_valid  : The instantaneous validation performed in
//                              WebRtc_ProcessBinarySpectrum().
//  - is_histogram_valid      : The histogram based validation.
//
// Return value:
//  - is_robust               : 1 - The candidate_delay is valid according to a
//                                  combination of the two inputs.
//                            : 0 - Otherwise.
static int RobustValidation(const BinaryDelayEstimator *self,
                            int candidate_delay,
                            int is_instantaneous_valid,
                            int is_histogram_valid) {
    int is_robust = 0;

    // The final robust validation is based on the two algorithms; 1) the
    // |is_instantaneous_valid| and 2) the histogram based with result stored in
    // |is_histogram_valid|.
    //   i) Before we actually have a valid estimate (|last_delay| == -2), we say
    //      a candidate is valid if either algorithm states so
    //      (|is_instantaneous_valid| OR |is_histogram_valid|).
    is_robust = (self->last_delay < 0) &&
                (is_instantaneous_valid || is_histogram_valid);
    //  ii) Otherwise, we need both algorithms to be certain
    //      (|is_instantaneous_valid| AND |is_histogram_valid|)
    is_robust |= is_instantaneous_valid && is_histogram_valid;
    // iii) With one exception, i.e., the histogram based algorithm can overrule
    //      the instantaneous one if |is_histogram_valid| = 1 and the histogram
    //      is significantly strong.
    is_robust |= is_histogram_valid &&
                 (self->histogram[candidate_delay] > self->last_delay_histogram);

    return is_robust;
}

void WebRtc_FreeBinaryDelayEstimatorFarend(BinaryDelayEstimatorFarend *self) {

    if (self == NULL) {
        return;
    }

    free(self->binary_far_history);
    self->binary_far_history = NULL;

    free(self->far_bit_counts);
    self->far_bit_counts = NULL;

    free(self);
}

BinaryDelayEstimatorFarend *WebRtc_CreateBinaryDelayEstimatorFarend(
        int history_size) {
    BinaryDelayEstimatorFarend *self = NULL;

    if (history_size > 1) {
        // Sanity conditions fulfilled.
        self = (BinaryDelayEstimatorFarend *) (
                malloc(sizeof(BinaryDelayEstimatorFarend)));
    }
    if (self == NULL) {
        return NULL;
    }

    self->history_size = 0;
    self->binary_far_history = NULL;
    self->far_bit_counts = NULL;
    if (WebRtc_AllocateFarendBufferMemory(self, history_size) == 0) {
        WebRtc_FreeBinaryDelayEstimatorFarend(self);
        self = NULL;
    }
    return self;
}

int WebRtc_AllocateFarendBufferMemory(BinaryDelayEstimatorFarend *self,
                                      int history_size) {
    assert(self);
    // (Re-)Allocate memory for history buffers.
    self->binary_far_history = (uint32_t *) (
            realloc(self->binary_far_history,
                    history_size * sizeof(*self->binary_far_history)));
    self->far_bit_counts = (int *) (
            realloc(self->far_bit_counts,
                    history_size * sizeof(*self->far_bit_counts)));
    if ((self->binary_far_history == NULL) || (self->far_bit_counts == NULL)) {
        history_size = 0;
    }
    // Fill with zeros if we have expanded the buffers.
    if (history_size > self->history_size) {
        int size_diff = history_size - self->history_size;
        memset(&self->binary_far_history[self->history_size],
               0,
               sizeof(*self->binary_far_history) * size_diff);
        memset(&self->far_bit_counts[self->history_size],
               0,
               sizeof(*self->far_bit_counts) * size_diff);
    }
    self->history_size = history_size;

    return self->history_size;
}

void WebRtc_InitBinaryDelayEstimatorFarend(BinaryDelayEstimatorFarend *self) {
    assert(self);
    memset(self->binary_far_history, 0, sizeof(uint32_t) * self->history_size);
    memset(self->far_bit_counts, 0, sizeof(int) * self->history_size);
}

void WebRtc_SoftResetBinaryDelayEstimatorFarend(
        BinaryDelayEstimatorFarend *self, int delay_shift) {
    int abs_shift = abs(delay_shift);
    int shift_size = 0;
    int dest_index = 0;
    int src_index = 0;
    int padding_index = 0;

    assert(self);
    shift_size = self->history_size - abs_shift;
    assert(shift_size > 0);
    if (delay_shift == 0) {
        return;
    } else if (delay_shift > 0) {
        dest_index = abs_shift;
    } else if (delay_shift < 0) {
        src_index = abs_shift;
        padding_index = shift_size;
    }

    // Shift and zero pad buffers.
    memmove(&self->binary_far_history[dest_index],
            &self->binary_far_history[src_index],
            sizeof(*self->binary_far_history) * shift_size);
    memset(&self->binary_far_history[padding_index], 0,
           sizeof(*self->binary_far_history) * abs_shift);
    memmove(&self->far_bit_counts[dest_index],
            &self->far_bit_counts[src_index],
            sizeof(*self->far_bit_counts) * shift_size);
    memset(&self->far_bit_counts[padding_index], 0,
           sizeof(*self->far_bit_counts) * abs_shift);
}

void WebRtc_AddBinaryFarSpectrum(BinaryDelayEstimatorFarend *handle,
                                 uint32_t binary_far_spectrum) {
    assert(handle);
    // Shift binary spectrum history and insert current |binary_far_spectrum|.
    memmove(&(handle->binary_far_history[1]), &(handle->binary_far_history[0]),
            (handle->history_size - 1) * sizeof(uint32_t));
    handle->binary_far_history[0] = binary_far_spectrum;

    // Shift history of far-end binary spectrum bit counts and insert bit count
    // of current |binary_far_spectrum|.
    memmove(&(handle->far_bit_counts[1]), &(handle->far_bit_counts[0]),
            (handle->history_size - 1) * sizeof(int));
    handle->far_bit_counts[0] = BitCount(binary_far_spectrum);
}

void WebRtc_FreeBinaryDelayEstimator(BinaryDelayEstimator *self) {

    if (self == NULL) {
        return;
    }

    free(self->mean_bit_counts);
    self->mean_bit_counts = NULL;

    free(self->bit_counts);
    self->bit_counts = NULL;

    free(self->binary_near_history);
    self->binary_near_history = NULL;

    free(self->histogram);
    self->histogram = NULL;

    // BinaryDelayEstimator does not have ownership of |farend|, hence we do not
    // free the memory here. That should be handled separately by the user.
    self->farend = NULL;

    free(self);
}

BinaryDelayEstimator *WebRtc_CreateBinaryDelayEstimator(
        BinaryDelayEstimatorFarend *farend, int max_lookahead) {
    BinaryDelayEstimator *self = NULL;

    if ((farend != NULL) && (max_lookahead >= 0)) {
        // Sanity conditions fulfilled.
        self = (BinaryDelayEstimator *) (
                malloc(sizeof(BinaryDelayEstimator)));
    }
    if (self == NULL) {
        return NULL;
    }

    self->farend = farend;
    self->near_history_size = max_lookahead + 1;
    self->history_size = 0;
    self->robust_validation_enabled = 0;  // Disabled by default.
    self->allowed_offset = 0;

    self->lookahead = max_lookahead;

    // Allocate memory for spectrum and history buffers.
    self->mean_bit_counts = NULL;
    self->bit_counts = NULL;
    self->histogram = NULL;
    self->binary_near_history = (uint32_t *) (
            malloc((max_lookahead + 1) * sizeof(*self->binary_near_history)));
    if (self->binary_near_history == NULL ||
        WebRtc_AllocateHistoryBufferMemory(self, farend->history_size) == 0) {
        WebRtc_FreeBinaryDelayEstimator(self);
        self = NULL;
    }

    return self;
}

int WebRtc_AllocateHistoryBufferMemory(BinaryDelayEstimator *self,
                                       int history_size) {
    BinaryDelayEstimatorFarend *far = self->farend;
    // (Re-)Allocate memory for spectrum and history buffers.
    if (history_size != far->history_size) {
        // Only update far-end buffers if we need.
        history_size = WebRtc_AllocateFarendBufferMemory(far, history_size);
    }
    // The extra array element in |mean_bit_counts| and |histogram| is a dummy
    // element only used while |last_delay| == -2, i.e., before we have a valid
    // estimate.
    self->mean_bit_counts = (int32_t *) (
            realloc(self->mean_bit_counts,
                    (history_size + 1) * sizeof(*self->mean_bit_counts)));
    self->bit_counts = (int32_t *) (
            realloc(self->bit_counts, history_size * sizeof(*self->bit_counts)));
    self->histogram = (float *) (
            realloc(self->histogram, (history_size + 1) * sizeof(*self->histogram)));

    if ((self->mean_bit_counts == NULL) ||
        (self->bit_counts == NULL) ||
        (self->histogram == NULL)) {
        history_size = 0;
    }
    // Fill with zeros if we have expanded the buffers.
    if (history_size > self->history_size) {
        int size_diff = history_size - self->history_size;
        memset(&self->mean_bit_counts[self->history_size],
               0,
               sizeof(*self->mean_bit_counts) * size_diff);
        memset(&self->bit_counts[self->history_size],
               0,
               sizeof(*self->bit_counts) * size_diff);
        memset(&self->histogram[self->history_size],
               0,
               sizeof(*self->histogram) * size_diff);
    }
    self->history_size = history_size;

    return self->history_size;
}

void WebRtc_InitBinaryDelayEstimator(BinaryDelayEstimator *self) {
    int i = 0;
    assert(self);

    memset(self->bit_counts, 0, sizeof(int32_t) * self->history_size);
    memset(self->binary_near_history,
           0,
           sizeof(uint32_t) * self->near_history_size);
    for (i = 0; i <= self->history_size; ++i) {
        self->mean_bit_counts[i] = (20 << 9);  // 20 in Q9.
        self->histogram[i] = 0.f;
    }
    self->minimum_probability = kMaxBitCountsQ9;  // 32 in Q9.
    self->last_delay_probability = (int) kMaxBitCountsQ9;  // 32 in Q9.

    // Default return value if we're unable to estimate. -1 is used for errors.
    self->last_delay = -2;

    self->last_candidate_delay = -2;
    self->compare_delay = self->history_size;
    self->candidate_hits = 0;
    self->last_delay_histogram = 0.f;
}

int WebRtc_SoftResetBinaryDelayEstimator(BinaryDelayEstimator *self,
                                         int delay_shift) {
    int lookahead = 0;
    assert(self);
    lookahead = self->lookahead;
    self->lookahead -= delay_shift;
    if (self->lookahead < 0) {
        self->lookahead = 0;
    }
    if (self->lookahead > self->near_history_size - 1) {
        self->lookahead = self->near_history_size - 1;
    }
    return lookahead - self->lookahead;
}

int WebRtc_ProcessBinarySpectrum(BinaryDelayEstimator *self,
                                 uint32_t binary_near_spectrum) {
    int i = 0;
    int candidate_delay = -1;
    int valid_candidate = 0;

    int32_t value_best_candidate = kMaxBitCountsQ9;
    int32_t value_worst_candidate = 0;
    int32_t valley_depth = 0;

    assert(self);
    if (self->farend->history_size != self->history_size) {
        // Non matching history sizes.
        return -1;
    }
    if (self->near_history_size > 1) {
        // If we apply lookahead, shift near-end binary spectrum history. Insert
        // current |binary_near_spectrum| and pull out the delayed one.
        memmove(&(self->binary_near_history[1]), &(self->binary_near_history[0]),
                (self->near_history_size - 1) * sizeof(uint32_t));
        self->binary_near_history[0] = binary_near_spectrum;
        binary_near_spectrum = self->binary_near_history[self->lookahead];
    }

    // Compare with delayed spectra and store the |bit_counts| for each delay.
    BitCountComparison(binary_near_spectrum, self->farend->binary_far_history,
                       self->history_size, self->bit_counts);

    // Update |mean_bit_counts|, which is the smoothed version of |bit_counts|.
    for (i = 0; i < self->history_size; i++) {
        // |bit_counts| is constrained to [0, 32], meaning we can smooth with a
        // factor up to 2^26. We use Q9.
        int32_t bit_count = (self->bit_counts[i] << 9);  // Q9.

        // Update |mean_bit_counts| only when far-end signal has something to
        // contribute. If |far_bit_counts| is zero the far-end signal is weak and
        // we likely have a poor echo condition, hence don't update.
        if (self->farend->far_bit_counts[i] > 0) {
            // Make number of right shifts piecewise linear w.r.t. |far_bit_counts|.
            int shifts = kShiftsAtZero;
            shifts -= (kShiftsLinearSlope * self->farend->far_bit_counts[i]) >> 4;
            WebRtc_MeanEstimatorFix(bit_count, shifts, &(self->mean_bit_counts[i]));
        }
    }

    // Find |candidate_delay|, |value_best_candidate| and |value_worst_candidate|
    // of |mean_bit_counts|.
    for (i = 0; i < self->history_size; i++) {
        if (self->mean_bit_counts[i] < value_best_candidate) {
            value_best_candidate = self->mean_bit_counts[i];
            candidate_delay = i;
        }
        if (self->mean_bit_counts[i] > value_worst_candidate) {
            value_worst_candidate = self->mean_bit_counts[i];
        }
    }
    valley_depth = value_worst_candidate - value_best_candidate;

    // The |value_best_candidate| is a good indicator on the probability of
    // |candidate_delay| being an accurate delay (a small |value_best_candidate|
    // means a good binary match). In the following sections we make a decision
    // whether to update |last_delay| or not.
    // 1) If the difference bit counts between the best and the worst delay
    //    candidates is too small we consider the situation to be unreliable and
    //    don't update |last_delay|.
    // 2) If the situation is reliable we update |last_delay| if the value of the
    //    best candidate delay has a value less than
    //     i) an adaptive threshold |minimum_probability|, or
    //    ii) this corresponding value |last_delay_probability|, but updated at
    //        this time instant.

    // Update |minimum_probability|.
    if ((self->minimum_probability > kProbabilityLowerLimit) &&
        (valley_depth > kProbabilityMinSpread)) {
        // The "hard" threshold can't be lower than 17 (in Q9).
        // The valley in the curve also has to be distinct, i.e., the
        // difference between |value_worst_candidate| and |value_best_candidate| has
        // to be large enough.
        int32_t threshold = value_best_candidate + kProbabilityOffset;
        if (threshold < kProbabilityLowerLimit) {
            threshold = kProbabilityLowerLimit;
        }
        if (self->minimum_probability > threshold) {
            self->minimum_probability = threshold;
        }
    }
    // Update |last_delay_probability|.
    // We use a Markov type model, i.e., a slowly increasing level over time.
    self->last_delay_probability++;
    // Validate |candidate_delay|.  We have a reliable instantaneous delay
    // estimate if
    //  1) The valley is distinct enough (|valley_depth| > |kProbabilityOffset|)
    // and
    //  2) The depth of the valley is deep enough
    //      (|value_best_candidate| < |minimum_probability|)
    //     and deeper than the best estimate so far
    //      (|value_best_candidate| < |last_delay_probability|)
    valid_candidate = ((valley_depth > kProbabilityOffset) &&
                       ((value_best_candidate < self->minimum_probability) ||
                        (value_best_candidate < self->last_delay_probability)));

    // Check for nonstationary farend signal.
    // may be wrong code?
    int non_stationary_farend = 0;
    int n = 0;
    for (n = 0; n < self->history_size; n++) {
        if (self->farend->far_bit_counts[n] > 0) {
            non_stationary_farend = 1;
            break;
        }
    }
/*  const bool non_stationary_farend =
          std::any_of(self->farend->far_bit_counts,
                      self->farend->far_bit_counts + self->history_size,
          [](int a) { return a > 0; });
*/
    if (non_stationary_farend) {
        // Only update the validation statistics when the farend is nonstationary
        // as the underlying estimates are otherwise frozen.
        UpdateRobustValidationStatistics(self, candidate_delay, valley_depth,
                                         value_best_candidate);
    }

    if (self->robust_validation_enabled) {
        int is_histogram_valid = HistogramBasedValidation(self, candidate_delay);
        valid_candidate = RobustValidation(self, candidate_delay, valid_candidate,
                                           is_histogram_valid);

    }

    // Only update the delay estimate when the farend is nonstationary and when
    // a valid delay candidate is available.
    if (non_stationary_farend && valid_candidate) {
        if (candidate_delay != self->last_delay) {
            self->last_delay_histogram =
                    (self->histogram[candidate_delay] > kLastHistogramMax ?
                     kLastHistogramMax : self->histogram[candidate_delay]);
            // Adjust the histogram if we made a change to |last_delay|, though it was
            // not the most likely one according to the histogram.
            if (self->histogram[candidate_delay] <
                self->histogram[self->compare_delay]) {
                self->histogram[self->compare_delay] = self->histogram[candidate_delay];
            }
        }
        self->last_delay = candidate_delay;
        if (value_best_candidate < self->last_delay_probability) {
            self->last_delay_probability = value_best_candidate;
        }
        self->compare_delay = self->last_delay;
    }

    return self->last_delay;
}

int WebRtc_binary_last_delay(BinaryDelayEstimator *self) {
    assert(self);
    return self->last_delay;
}

float WebRtc_binary_last_delay_quality(BinaryDelayEstimator *self) {
    float quality = 0;
    assert(self);

    if (self->robust_validation_enabled) {
        // Simply a linear function of the histogram height at delay estimate.
        quality = self->histogram[self->compare_delay] / kHistogramMax;
    } else {
        // Note that |last_delay_probability| states how deep the minimum of the
        // cost function is, so it is rather an error probability.
        quality = (float) (kMaxBitCountsQ9 - self->last_delay_probability) /
                  kMaxBitCountsQ9;
        if (quality < 0) {
            quality = 0;
        }
    }
    return quality;
}

void WebRtc_MeanEstimatorFix(int32_t new_value,
                             int factor,
                             int32_t *mean_value) {
    int32_t diff = new_value - *mean_value;

    // mean_new = mean_value + ((new_value - mean_value) >> factor);
    if (diff < 0) {
        diff = -((-diff) >> factor);
    } else {
        diff = (diff >> factor);
    }
    *mean_value += diff;
}


// Only bit |kBandFirst| through bit |kBandLast| are processed and
// |kBandFirst| - |kBandLast| must be < 32.
enum {
    kBandFirst = 12
};
enum {
    kBandLast = 43
};

static __inline uint32_t SetBit(uint32_t in, int pos) {
    uint32_t mask = (1 << pos);
    uint32_t out = (in | mask);

    return out;
}

// Calculates the mean recursively. Same version as WebRtc_MeanEstimatorFix(),
// but for float.
//
// Inputs:
//    - new_value             : New additional value.
//    - scale                 : Scale for smoothing (should be less than 1.0).
//
// Input/Output:
//    - mean_value            : Pointer to the mean value for updating.
//
static void MeanEstimatorFloat(float new_value,
                               float scale,
                               float *mean_value) {
    assert(scale < 1.0f);
    *mean_value += (new_value - *mean_value) * scale;
}

// Computes the binary spectrum by comparing the input |spectrum| with a
// |threshold_spectrum|. Float and fixed point versions.
//
// Inputs:
//      - spectrum            : Spectrum of which the binary spectrum should be
//                              calculated.
//      - threshold_spectrum  : Threshold spectrum with which the input
//                              spectrum is compared.
// Return:
//      - out                 : Binary spectrum.
//
static uint32_t BinarySpectrumFix(const uint16_t *spectrum,
                                  SpectrumType *threshold_spectrum,
                                  int q_domain,
                                  int *threshold_initialized) {
    int i = 0;
    uint32_t out = 0;

    assert(q_domain < 16);

    if (!(*threshold_initialized)) {
        // Set the |threshold_spectrum| to half the input |spectrum| as starting
        // value. This speeds up the convergence.
        for (i = kBandFirst; i <= kBandLast; i++) {
            if (spectrum[i] > 0) {
                // Convert input spectrum from Q(|q_domain|) to Q15.
                int32_t spectrum_q15 = ((int32_t) spectrum[i]) << (15 - q_domain);
                threshold_spectrum[i].int32_ = (spectrum_q15 >> 1);
                *threshold_initialized = 1;
            }
        }
    }
    for (i = kBandFirst; i <= kBandLast; i++) {
        // Convert input spectrum from Q(|q_domain|) to Q15.
        int32_t spectrum_q15 = ((int32_t) spectrum[i]) << (15 - q_domain);
        // Update the |threshold_spectrum|.
        WebRtc_MeanEstimatorFix(spectrum_q15, 6, &(threshold_spectrum[i].int32_));
        // Convert |spectrum| at current frequency bin to a binary value.
        if (spectrum_q15 > threshold_spectrum[i].int32_) {
            out = SetBit(out, i - kBandFirst);
        }
    }

    return out;
}

static uint32_t BinarySpectrumFloat(const float *spectrum,
                                    SpectrumType *threshold_spectrum,
                                    int *threshold_initialized) {
    int i = 0;
    uint32_t out = 0;
    const float kScale = 1 / 64.0;

    if (!(*threshold_initialized)) {
        // Set the |threshold_spectrum| to half the input |spectrum| as starting
        // value. This speeds up the convergence.
        for (i = kBandFirst; i <= kBandLast; i++) {
            if (spectrum[i] > 0.0f) {
                threshold_spectrum[i].float_ = (spectrum[i] / 2);
                *threshold_initialized = 1;
            }
        }
    }

    for (i = kBandFirst; i <= kBandLast; i++) {
        // Update the |threshold_spectrum|.
        MeanEstimatorFloat(spectrum[i], kScale, &(threshold_spectrum[i].float_));
        // Convert |spectrum| at current frequency bin to a binary value.
        if (spectrum[i] > threshold_spectrum[i].float_) {
            out = SetBit(out, i - kBandFirst);
        }
    }

    return out;
}

void WebRtc_FreeDelayEstimatorFarend(void *handle) {
    DelayEstimatorFarend *self = (DelayEstimatorFarend *) handle;

    if (handle == NULL) {
        return;
    }

    free(self->mean_far_spectrum);
    self->mean_far_spectrum = NULL;

    WebRtc_FreeBinaryDelayEstimatorFarend(self->binary_farend);
    self->binary_farend = NULL;

    free(self);
}

void *WebRtc_CreateDelayEstimatorFarend(int spectrum_size, int history_size) {
    DelayEstimatorFarend *self = NULL;

    // Check if the sub band used in the delay estimation is small enough to fit
    // the binary spectra in a uint32_t.
    // static_assert(kBandLast - kBandFirst < 32, "");

    if (spectrum_size >= kBandLast) {
        self = (DelayEstimatorFarend *) (
                malloc(sizeof(DelayEstimatorFarend)));
    }

    if (self != NULL) {
        int memory_fail = 0;

        // Allocate memory for the binary far-end spectrum handling.
        self->binary_farend = WebRtc_CreateBinaryDelayEstimatorFarend(history_size);
        memory_fail |= (self->binary_farend == NULL);

        // Allocate memory for spectrum buffers.
        self->mean_far_spectrum = (SpectrumType *) (malloc(spectrum_size * sizeof(SpectrumType)));
        memory_fail |= (self->mean_far_spectrum == NULL);

        self->spectrum_size = spectrum_size;

        if (memory_fail) {
            WebRtc_FreeDelayEstimatorFarend(self);
            self = NULL;
        }
    }

    return self;
}

int WebRtc_InitDelayEstimatorFarend(void *handle) {
    DelayEstimatorFarend *self = (DelayEstimatorFarend *) handle;

    if (self == NULL) {
        return -1;
    }

    // Initialize far-end part of binary delay estimator.
    WebRtc_InitBinaryDelayEstimatorFarend(self->binary_farend);

    // Set averaged far and near end spectra to zero.
    memset(self->mean_far_spectrum, 0,
           sizeof(SpectrumType) * self->spectrum_size);
    // Reset initialization indicators.
    self->far_spectrum_initialized = 0;

    return 0;
}

void WebRtc_SoftResetDelayEstimatorFarend(void *handle, int delay_shift) {
    DelayEstimatorFarend *self = (DelayEstimatorFarend *) handle;
    assert(self);
    WebRtc_SoftResetBinaryDelayEstimatorFarend(self->binary_farend, delay_shift);
}

int WebRtc_AddFarSpectrumFix(void *handle,
                             const uint16_t *far_spectrum,
                             int spectrum_size,
                             int far_q) {
    DelayEstimatorFarend *self = (DelayEstimatorFarend *) handle;
    uint32_t binary_spectrum = 0;

    if (self == NULL) {
        return -1;
    }
    if (far_spectrum == NULL) {
        // Empty far end spectrum.
        return -1;
    }
    if (spectrum_size != self->spectrum_size) {
        // Data sizes don't match.
        return -1;
    }
    if (far_q > 15) {
        // If |far_q| is larger than 15 we cannot guarantee no wrap around.
        return -1;
    }

    // Get binary spectrum.
    binary_spectrum = BinarySpectrumFix(far_spectrum, self->mean_far_spectrum,
                                        far_q, &(self->far_spectrum_initialized));
    WebRtc_AddBinaryFarSpectrum(self->binary_farend, binary_spectrum);

    return 0;
}

int WebRtc_AddFarSpectrumFloat(void *handle,
                               const float *far_spectrum,
                               int spectrum_size) {
    DelayEstimatorFarend *self = (DelayEstimatorFarend *) handle;
    uint32_t binary_spectrum = 0;

    if (self == NULL) {
        return -1;
    }
    if (far_spectrum == NULL) {
        // Empty far end spectrum.
        return -1;
    }
    if (spectrum_size != self->spectrum_size) {
        // Data sizes don't match.
        return -1;
    }

    // Get binary spectrum.
    binary_spectrum = BinarySpectrumFloat(far_spectrum, self->mean_far_spectrum,
                                          &(self->far_spectrum_initialized));
    WebRtc_AddBinaryFarSpectrum(self->binary_farend, binary_spectrum);

    return 0;
}

void WebRtc_FreeDelayEstimator(void *handle) {
    DelayEstimator *self = (DelayEstimator *) handle;

    if (handle == NULL) {
        return;
    }

    free(self->mean_near_spectrum);
    self->mean_near_spectrum = NULL;

    WebRtc_FreeBinaryDelayEstimator(self->binary_handle);
    self->binary_handle = NULL;

    free(self);
}

void *WebRtc_CreateDelayEstimator(void *farend_handle, int max_lookahead) {
    DelayEstimator *self = NULL;
    DelayEstimatorFarend *farend = (DelayEstimatorFarend *) farend_handle;

    if (farend_handle != NULL) {
        self = (DelayEstimator *) (malloc(sizeof(DelayEstimator)));
    }

    if (self != NULL) {
        int memory_fail = 0;

        // Allocate memory for the farend spectrum handling.
        self->binary_handle =
                WebRtc_CreateBinaryDelayEstimator(farend->binary_farend, max_lookahead);
        memory_fail |= (self->binary_handle == NULL);

        // Allocate memory for spectrum buffers.
        self->mean_near_spectrum = (SpectrumType *) (
                malloc(farend->spectrum_size * sizeof(SpectrumType)));
        memory_fail |= (self->mean_near_spectrum == NULL);

        self->spectrum_size = farend->spectrum_size;

        if (memory_fail) {
            WebRtc_FreeDelayEstimator(self);
            self = NULL;
        }
    }

    return self;
}

int WebRtc_InitDelayEstimator(void *handle) {
    DelayEstimator *self = (DelayEstimator *) handle;

    if (self == NULL) {
        return -1;
    }

    // Initialize binary delay estimator.
    WebRtc_InitBinaryDelayEstimator(self->binary_handle);

    // Set averaged far and near end spectra to zero.
    memset(self->mean_near_spectrum, 0,
           sizeof(SpectrumType) * self->spectrum_size);
    // Reset initialization indicators.
    self->near_spectrum_initialized = 0;

    return 0;
}

int WebRtc_SoftResetDelayEstimator(void *handle, int delay_shift) {
    DelayEstimator *self = (DelayEstimator *) handle;
    assert(self);
    return WebRtc_SoftResetBinaryDelayEstimator(self->binary_handle, delay_shift);
}

int WebRtc_set_history_size(void *handle, int history_size) {
    DelayEstimator *self = (DelayEstimator *) (handle);

    if ((self == NULL) || (history_size <= 1)) {
        return -1;
    }
    return WebRtc_AllocateHistoryBufferMemory(self->binary_handle, history_size);
}

int WebRtc_history_size(const void *handle) {
    const DelayEstimator *self = (const DelayEstimator *) (handle);

    if (self == NULL) {
        return -1;
    }
    if (self->binary_handle->farend->history_size !=
        self->binary_handle->history_size) {
        // Non matching history sizes.
        return -1;
    }
    return self->binary_handle->history_size;
}

int WebRtc_set_lookahead(void *handle, int lookahead) {
    DelayEstimator *self = (DelayEstimator *) handle;
    assert(self);
    assert(self->binary_handle);
    if ((lookahead > self->binary_handle->near_history_size - 1) ||
        (lookahead < 0)) {
        return -1;
    }
    self->binary_handle->lookahead = lookahead;
    return self->binary_handle->lookahead;
}

int WebRtc_lookahead(void *handle) {
    DelayEstimator *self = (DelayEstimator *) handle;
    assert(self);
    assert(self->binary_handle);
    return self->binary_handle->lookahead;
}

int WebRtc_set_allowed_offset(void *handle, int allowed_offset) {
    DelayEstimator *self = (DelayEstimator *) handle;

    if ((self == NULL) || (allowed_offset < 0)) {
        return -1;
    }
    self->binary_handle->allowed_offset = allowed_offset;
    return 0;
}

int WebRtc_get_allowed_offset(const void *handle) {
    const DelayEstimator *self = (const DelayEstimator *) handle;

    if (self == NULL) {
        return -1;
    }
    return self->binary_handle->allowed_offset;
}

int WebRtc_enable_robust_validation(void *handle, int enable) {
    DelayEstimator *self = (DelayEstimator *) handle;

    if (self == NULL) {
        return -1;
    }
    if ((enable < 0) || (enable > 1)) {
        return -1;
    }
    assert(self->binary_handle);
    self->binary_handle->robust_validation_enabled = enable;
    return 0;
}

int WebRtc_is_robust_validation_enabled(const void *handle) {
    const DelayEstimator *self = (const DelayEstimator *) handle;

    if (self == NULL) {
        return -1;
    }
    return self->binary_handle->robust_validation_enabled;
}

int WebRtc_DelayEstimatorProcessFix(void *handle,
                                    const uint16_t *near_spectrum,
                                    int spectrum_size,
                                    int near_q) {
    DelayEstimator *self = (DelayEstimator *) handle;
    uint32_t binary_spectrum = 0;

    if (self == NULL) {
        return -1;
    }
    if (near_spectrum == NULL) {
        // Empty near end spectrum.
        return -1;
    }
    if (spectrum_size != self->spectrum_size) {
        // Data sizes don't match.
        return -1;
    }
    if (near_q > 15) {
        // If |near_q| is larger than 15 we cannot guarantee no wrap around.
        return -1;
    }

    // Get binary spectra.
    binary_spectrum = BinarySpectrumFix(near_spectrum,
                                        self->mean_near_spectrum,
                                        near_q,
                                        &(self->near_spectrum_initialized));

    return WebRtc_ProcessBinarySpectrum(self->binary_handle, binary_spectrum);
}

int WebRtc_DelayEstimatorProcessFloat(void *handle,
                                      const float *near_spectrum,
                                      int spectrum_size) {
    DelayEstimator *self = (DelayEstimator *) handle;
    uint32_t binary_spectrum = 0;

    if (self == NULL) {
        return -1;
    }
    if (near_spectrum == NULL) {
        // Empty near end spectrum.
        return -1;
    }
    if (spectrum_size != self->spectrum_size) {
        // Data sizes don't match.
        return -1;
    }

    // Get binary spectrum.
    binary_spectrum = BinarySpectrumFloat(near_spectrum, self->mean_near_spectrum,
                                          &(self->near_spectrum_initialized));

    return WebRtc_ProcessBinarySpectrum(self->binary_handle, binary_spectrum);
}

int WebRtc_last_delay(void *handle) {
    DelayEstimator *self = (DelayEstimator *) handle;

    if (self == NULL) {
        return -1;
    }

    return WebRtc_binary_last_delay(self->binary_handle);
}

float WebRtc_last_delay_quality(void *handle) {
    DelayEstimator *self = (DelayEstimator *) handle;
    assert(self);
    return WebRtc_binary_last_delay_quality(self->binary_handle);
}

#define BUF_SIZE_FRAMES 50 // buffer size (frames)
// Maximum length of resampled signal. Must be an integer multiple of frames
// (ceil(1/(1 + MIN_SKEW)*2) + 1)*FRAME_LEN
// The factor of 2 handles wb, and the + 1 is as a safety margin
#define MAX_RESAMP_LEN (5 * FRAME_LEN)

static const size_t kBufSizeSamp = BUF_SIZE_FRAMES * FRAME_LEN; // buffer size (samples)
static const int kSampMsNb = 8; // samples per ms in nb
// Target suppression levels for nlp modes
// log{0.001, 0.00001, 0.00000001}
static const int kInitCheck = 42;

typedef struct {
    int sampFreq;
    int scSampFreq;
    short bufSizeStart;
    int knownDelay;

    // Stores the last frame added to the farend buffer
    short farendOld[2][FRAME_LEN];
    short initFlag; // indicates if AEC has been initialized

    // Variables used for averaging far end buffer size
    short counter;
    short sum;
    short firstVal;
    short checkBufSizeCtr;

    // Variables used for delay shifts
    short msInSndCardBuf;
    short filtDelay;
    int timeForDelayChange;
    int ECstartup;
    int checkBuffSize;
    int delayChange;
    short lastDelayDiff;

    int16_t echoMode;

#ifdef AEC_DEBUG
    FILE *bufFile;
    FILE *delayFile;
    FILE *preCompFile;
    FILE *postCompFile;
#endif // AEC_DEBUG
    // Structures
    RingBuffer *farendBuf;

    AecmCore *aecmCore;
} AecMobile;

/* Tables for data buffer indexes that are bit reversed and thus need to be
 * swapped. Note that, index_7[{0, 2, 4, ...}] are for the left side of the swap
 * operations, while index_7[{1, 3, 5, ...}] are for the right side of the
 * operation. Same for index_8.
 */

/* Indexes for the case of stages == 7. */
static const int16_t index_7[112] = {
        1, 64, 2, 32, 3, 96, 4, 16, 5, 80, 6, 48, 7, 112, 9, 72, 10, 40, 11, 104,
        12, 24, 13, 88, 14, 56, 15, 120, 17, 68, 18, 36, 19, 100, 21, 84, 22, 52,
        23, 116, 25, 76, 26, 44, 27, 108, 29, 92, 30, 60, 31, 124, 33, 66, 35, 98,
        37, 82, 38, 50, 39, 114, 41, 74, 43, 106, 45, 90, 46, 58, 47, 122, 49, 70,
        51, 102, 53, 86, 55, 118, 57, 78, 59, 110, 61, 94, 63, 126, 67, 97, 69,
        81, 71, 113, 75, 105, 77, 89, 79, 121, 83, 101, 87, 117, 91, 109, 95, 125,
        103, 115, 111, 123
};

/* Indexes for the case of stages == 8. */
static const int16_t index_8[240] = {
        1, 128, 2, 64, 3, 192, 4, 32, 5, 160, 6, 96, 7, 224, 8, 16, 9, 144, 10, 80,
        11, 208, 12, 48, 13, 176, 14, 112, 15, 240, 17, 136, 18, 72, 19, 200, 20,
        40, 21, 168, 22, 104, 23, 232, 25, 152, 26, 88, 27, 216, 28, 56, 29, 184,
        30, 120, 31, 248, 33, 132, 34, 68, 35, 196, 37, 164, 38, 100, 39, 228, 41,
        148, 42, 84, 43, 212, 44, 52, 45, 180, 46, 116, 47, 244, 49, 140, 50, 76,
        51, 204, 53, 172, 54, 108, 55, 236, 57, 156, 58, 92, 59, 220, 61, 188, 62,
        124, 63, 252, 65, 130, 67, 194, 69, 162, 70, 98, 71, 226, 73, 146, 74, 82,
        75, 210, 77, 178, 78, 114, 79, 242, 81, 138, 83, 202, 85, 170, 86, 106, 87,
        234, 89, 154, 91, 218, 93, 186, 94, 122, 95, 250, 97, 134, 99, 198, 101,
        166, 103, 230, 105, 150, 107, 214, 109, 182, 110, 118, 111, 246, 113, 142,
        115, 206, 117, 174, 119, 238, 121, 158, 123, 222, 125, 190, 127, 254, 131,
        193, 133, 161, 135, 225, 137, 145, 139, 209, 141, 177, 143, 241, 147, 201,
        149, 169, 151, 233, 155, 217, 157, 185, 159, 249, 163, 197, 167, 229, 171,
        213, 173, 181, 175, 245, 179, 205, 183, 237, 187, 221, 191, 253, 199, 227,
        203, 211, 207, 243, 215, 235, 223, 251, 239, 247
};

void WebRtcSpl_ComplexBitReverse(int16_t *__restrict complex_data, int stages) {
    /* For any specific value of stages, we know exactly the indexes that are
     * bit reversed. Currently (Feb. 2012) in WebRTC the only possible values of
     * stages are 7 and 8, so we use tables to save unnecessary iterations and
     * calculations for these two cases.
     */
    if (stages == 7 || stages == 8) {
        int m = 0;
        int length = 112;
        const int16_t *index = index_7;

        if (stages == 8) {
            length = 240;
            index = index_8;
        }

        /* Decimation in time. Swap the elements with bit-reversed indexes. */
        for (m = 0; m < length; m += 2) {
            /* We declare a int32_t* type pointer, to load both the 16-bit real
             * and imaginary elements from complex_data in one instruction, reducing
             * complexity.
             */
            int32_t *complex_data_ptr = (int32_t *) complex_data;
            int32_t temp = 0;

            temp = complex_data_ptr[index[m]];  /* Real and imaginary */
            complex_data_ptr[index[m]] = complex_data_ptr[index[m + 1]];
            complex_data_ptr[index[m + 1]] = temp;
        }
    } else {
        int m = 0, mr = 0, l = 0;
        int n = 1 << stages;
        int nn = n - 1;

        /* Decimation in time - re-order data */
        for (m = 1; m <= nn; ++m) {
            int32_t *complex_data_ptr = (int32_t *) complex_data;
            int32_t temp = 0;

            /* Find out indexes that are bit-reversed. */
            l = n;
            do {
                l >>= 1;
            } while (l > nn - mr);
            mr = (mr & (l - 1)) + l;

            if (mr <= m) {
                continue;
            }

            /* Swap the elements with bit-reversed indexes.
             * This is similar to the loop in the stages == 7 or 8 cases.
             */
            temp = complex_data_ptr[m];  /* Real and imaginary */
            complex_data_ptr[m] = complex_data_ptr[mr];
            complex_data_ptr[mr] = temp;
        }
    }
}

#define CFFTSFT 14
#define CFFTRND 1
#define CFFTRND2 16384

#define CIFFTSFT 14
#define CIFFTRND 1


static const int16_t kSinTable1024[] = {
        0, 201, 402, 603, 804, 1005, 1206, 1406,
        1607, 1808, 2009, 2209, 2410, 2610, 2811, 3011,
        3211, 3411, 3611, 3811, 4011, 4210, 4409, 4608,
        4807, 5006, 5205, 5403, 5601, 5799, 5997, 6195,
        6392, 6589, 6786, 6982, 7179, 7375, 7571, 7766,
        7961, 8156, 8351, 8545, 8739, 8932, 9126, 9319,
        9511, 9703, 9895, 10087, 10278, 10469, 10659, 10849,
        11038, 11227, 11416, 11604, 11792, 11980, 12166, 12353,
        12539, 12724, 12909, 13094, 13278, 13462, 13645, 13827,
        14009, 14191, 14372, 14552, 14732, 14911, 15090, 15268,
        15446, 15623, 15799, 15975, 16150, 16325, 16499, 16672,
        16845, 17017, 17189, 17360, 17530, 17699, 17868, 18036,
        18204, 18371, 18537, 18702, 18867, 19031, 19194, 19357,
        19519, 19680, 19840, 20000, 20159, 20317, 20474, 20631,
        20787, 20942, 21096, 21249, 21402, 21554, 21705, 21855,
        22004, 22153, 22301, 22448, 22594, 22739, 22883, 23027,
        23169, 23311, 23452, 23592, 23731, 23869, 24006, 24143,
        24278, 24413, 24546, 24679, 24811, 24942, 25072, 25201,
        25329, 25456, 25582, 25707, 25831, 25954, 26077, 26198,
        26318, 26437, 26556, 26673, 26789, 26905, 27019, 27132,
        27244, 27355, 27466, 27575, 27683, 27790, 27896, 28001,
        28105, 28208, 28309, 28410, 28510, 28608, 28706, 28802,
        28897, 28992, 29085, 29177, 29268, 29358, 29446, 29534,
        29621, 29706, 29790, 29873, 29955, 30036, 30116, 30195,
        30272, 30349, 30424, 30498, 30571, 30643, 30713, 30783,
        30851, 30918, 30984, 31049, 31113, 31175, 31236, 31297,
        31356, 31413, 31470, 31525, 31580, 31633, 31684, 31735,
        31785, 31833, 31880, 31926, 31970, 32014, 32056, 32097,
        32137, 32176, 32213, 32249, 32284, 32318, 32350, 32382,
        32412, 32441, 32468, 32495, 32520, 32544, 32567, 32588,
        32609, 32628, 32646, 32662, 32678, 32692, 32705, 32717,
        32727, 32736, 32744, 32751, 32757, 32761, 32764, 32766,
        32767, 32766, 32764, 32761, 32757, 32751, 32744, 32736,
        32727, 32717, 32705, 32692, 32678, 32662, 32646, 32628,
        32609, 32588, 32567, 32544, 32520, 32495, 32468, 32441,
        32412, 32382, 32350, 32318, 32284, 32249, 32213, 32176,
        32137, 32097, 32056, 32014, 31970, 31926, 31880, 31833,
        31785, 31735, 31684, 31633, 31580, 31525, 31470, 31413,
        31356, 31297, 31236, 31175, 31113, 31049, 30984, 30918,
        30851, 30783, 30713, 30643, 30571, 30498, 30424, 30349,
        30272, 30195, 30116, 30036, 29955, 29873, 29790, 29706,
        29621, 29534, 29446, 29358, 29268, 29177, 29085, 28992,
        28897, 28802, 28706, 28608, 28510, 28410, 28309, 28208,
        28105, 28001, 27896, 27790, 27683, 27575, 27466, 27355,
        27244, 27132, 27019, 26905, 26789, 26673, 26556, 26437,
        26318, 26198, 26077, 25954, 25831, 25707, 25582, 25456,
        25329, 25201, 25072, 24942, 24811, 24679, 24546, 24413,
        24278, 24143, 24006, 23869, 23731, 23592, 23452, 23311,
        23169, 23027, 22883, 22739, 22594, 22448, 22301, 22153,
        22004, 21855, 21705, 21554, 21402, 21249, 21096, 20942,
        20787, 20631, 20474, 20317, 20159, 20000, 19840, 19680,
        19519, 19357, 19194, 19031, 18867, 18702, 18537, 18371,
        18204, 18036, 17868, 17699, 17530, 17360, 17189, 17017,
        16845, 16672, 16499, 16325, 16150, 15975, 15799, 15623,
        15446, 15268, 15090, 14911, 14732, 14552, 14372, 14191,
        14009, 13827, 13645, 13462, 13278, 13094, 12909, 12724,
        12539, 12353, 12166, 11980, 11792, 11604, 11416, 11227,
        11038, 10849, 10659, 10469, 10278, 10087, 9895, 9703,
        9511, 9319, 9126, 8932, 8739, 8545, 8351, 8156,
        7961, 7766, 7571, 7375, 7179, 6982, 6786, 6589,
        6392, 6195, 5997, 5799, 5601, 5403, 5205, 5006,
        4807, 4608, 4409, 4210, 4011, 3811, 3611, 3411,
        3211, 3011, 2811, 2610, 2410, 2209, 2009, 1808,
        1607, 1406, 1206, 1005, 804, 603, 402, 201,
        0, -201, -402, -603, -804, -1005, -1206, -1406,
        -1607, -1808, -2009, -2209, -2410, -2610, -2811, -3011,
        -3211, -3411, -3611, -3811, -4011, -4210, -4409, -4608,
        -4807, -5006, -5205, -5403, -5601, -5799, -5997, -6195,
        -6392, -6589, -6786, -6982, -7179, -7375, -7571, -7766,
        -7961, -8156, -8351, -8545, -8739, -8932, -9126, -9319,
        -9511, -9703, -9895, -10087, -10278, -10469, -10659, -10849,
        -11038, -11227, -11416, -11604, -11792, -11980, -12166, -12353,
        -12539, -12724, -12909, -13094, -13278, -13462, -13645, -13827,
        -14009, -14191, -14372, -14552, -14732, -14911, -15090, -15268,
        -15446, -15623, -15799, -15975, -16150, -16325, -16499, -16672,
        -16845, -17017, -17189, -17360, -17530, -17699, -17868, -18036,
        -18204, -18371, -18537, -18702, -18867, -19031, -19194, -19357,
        -19519, -19680, -19840, -20000, -20159, -20317, -20474, -20631,
        -20787, -20942, -21096, -21249, -21402, -21554, -21705, -21855,
        -22004, -22153, -22301, -22448, -22594, -22739, -22883, -23027,
        -23169, -23311, -23452, -23592, -23731, -23869, -24006, -24143,
        -24278, -24413, -24546, -24679, -24811, -24942, -25072, -25201,
        -25329, -25456, -25582, -25707, -25831, -25954, -26077, -26198,
        -26318, -26437, -26556, -26673, -26789, -26905, -27019, -27132,
        -27244, -27355, -27466, -27575, -27683, -27790, -27896, -28001,
        -28105, -28208, -28309, -28410, -28510, -28608, -28706, -28802,
        -28897, -28992, -29085, -29177, -29268, -29358, -29446, -29534,
        -29621, -29706, -29790, -29873, -29955, -30036, -30116, -30195,
        -30272, -30349, -30424, -30498, -30571, -30643, -30713, -30783,
        -30851, -30918, -30984, -31049, -31113, -31175, -31236, -31297,
        -31356, -31413, -31470, -31525, -31580, -31633, -31684, -31735,
        -31785, -31833, -31880, -31926, -31970, -32014, -32056, -32097,
        -32137, -32176, -32213, -32249, -32284, -32318, -32350, -32382,
        -32412, -32441, -32468, -32495, -32520, -32544, -32567, -32588,
        -32609, -32628, -32646, -32662, -32678, -32692, -32705, -32717,
        -32727, -32736, -32744, -32751, -32757, -32761, -32764, -32766,
        -32767, -32766, -32764, -32761, -32757, -32751, -32744, -32736,
        -32727, -32717, -32705, -32692, -32678, -32662, -32646, -32628,
        -32609, -32588, -32567, -32544, -32520, -32495, -32468, -32441,
        -32412, -32382, -32350, -32318, -32284, -32249, -32213, -32176,
        -32137, -32097, -32056, -32014, -31970, -31926, -31880, -31833,
        -31785, -31735, -31684, -31633, -31580, -31525, -31470, -31413,
        -31356, -31297, -31236, -31175, -31113, -31049, -30984, -30918,
        -30851, -30783, -30713, -30643, -30571, -30498, -30424, -30349,
        -30272, -30195, -30116, -30036, -29955, -29873, -29790, -29706,
        -29621, -29534, -29446, -29358, -29268, -29177, -29085, -28992,
        -28897, -28802, -28706, -28608, -28510, -28410, -28309, -28208,
        -28105, -28001, -27896, -27790, -27683, -27575, -27466, -27355,
        -27244, -27132, -27019, -26905, -26789, -26673, -26556, -26437,
        -26318, -26198, -26077, -25954, -25831, -25707, -25582, -25456,
        -25329, -25201, -25072, -24942, -24811, -24679, -24546, -24413,
        -24278, -24143, -24006, -23869, -23731, -23592, -23452, -23311,
        -23169, -23027, -22883, -22739, -22594, -22448, -22301, -22153,
        -22004, -21855, -21705, -21554, -21402, -21249, -21096, -20942,
        -20787, -20631, -20474, -20317, -20159, -20000, -19840, -19680,
        -19519, -19357, -19194, -19031, -18867, -18702, -18537, -18371,
        -18204, -18036, -17868, -17699, -17530, -17360, -17189, -17017,
        -16845, -16672, -16499, -16325, -16150, -15975, -15799, -15623,
        -15446, -15268, -15090, -14911, -14732, -14552, -14372, -14191,
        -14009, -13827, -13645, -13462, -13278, -13094, -12909, -12724,
        -12539, -12353, -12166, -11980, -11792, -11604, -11416, -11227,
        -11038, -10849, -10659, -10469, -10278, -10087, -9895, -9703,
        -9511, -9319, -9126, -8932, -8739, -8545, -8351, -8156,
        -7961, -7766, -7571, -7375, -7179, -6982, -6786, -6589,
        -6392, -6195, -5997, -5799, -5601, -5403, -5205, -5006,
        -4807, -4608, -4409, -4210, -4011, -3811, -3611, -3411,
        -3211, -3011, -2811, -2610, -2410, -2209, -2009, -1808,
        -1607, -1406, -1206, -1005, -804, -603, -402, -201
};

int WebRtcSpl_ComplexFFT(int16_t frfi[], int stages, int mode) {
    int i, j, l, k, istep, n, m;
    int16_t wr, wi;
    int32_t tr32, ti32, qr32, qi32;

    /* The 1024-value is a constant given from the size of kSinTable1024[],
     * and should not be changed depending on the input parameter 'stages'
     */
    n = 1 << stages;
    if (n > 1024)
        return -1;

    l = 1;
    k = 10 - 1; /* Constant for given kSinTable1024[]. Do not change
         depending on the input parameter 'stages' */

    if (mode == 0) {
        // mode==0: Low-complexity and Low-accuracy mode
        while (l < n) {
            istep = l << 1;

            for (m = 0; m < l; ++m) {
                j = m << k;

                /* The 256-value is a constant given as 1/4 of the size of
                 * kSinTable1024[], and should not be changed depending on the input
                 * parameter 'stages'. It will result in 0 <= j < N_SINE_WAVE/2
                 */
                wr = kSinTable1024[j + 256];
                wi = -kSinTable1024[j];

                for (i = m; i < n; i += istep) {
                    j = i + l;

                    tr32 = (wr * frfi[2 * j] - wi * frfi[2 * j + 1]) >> 15;

                    ti32 = (wr * frfi[2 * j + 1] + wi * frfi[2 * j]) >> 15;

                    qr32 = (int32_t) frfi[2 * i];
                    qi32 = (int32_t) frfi[2 * i + 1];
                    frfi[2 * j] = (int16_t) ((qr32 - tr32) >> 1);
                    frfi[2 * j + 1] = (int16_t) ((qi32 - ti32) >> 1);
                    frfi[2 * i] = (int16_t) ((qr32 + tr32) >> 1);
                    frfi[2 * i + 1] = (int16_t) ((qi32 + ti32) >> 1);
                }
            }

            --k;
            l = istep;

        }

    } else {
        // mode==1: High-complexity and High-accuracy mode
        while (l < n) {
            istep = l << 1;

            for (m = 0; m < l; ++m) {
                j = m << k;

                /* The 256-value is a constant given as 1/4 of the size of
                 * kSinTable1024[], and should not be changed depending on the input
                 * parameter 'stages'. It will result in 0 <= j < N_SINE_WAVE/2
                 */
                wr = kSinTable1024[j + 256];
                wi = -kSinTable1024[j];

#ifdef WEBRTC_ARCH_ARM_V7
                int32_t wri = 0;
                __asm __volatile("pkhbt %0, %1, %2, lsl #16" : "=r"(wri) :
                    "r"((int32_t)wr), "r"((int32_t)wi));
#endif

                for (i = m; i < n; i += istep) {
                    j = i + l;

#ifdef WEBRTC_ARCH_ARM_V7
                    register int32_t frfi_r;
                    __asm __volatile(
                        "pkhbt %[frfi_r], %[frfi_even], %[frfi_odd],"
                        " lsl #16\n\t"
                        "smlsd %[tr32], %[wri], %[frfi_r], %[cfftrnd]\n\t"
                        "smladx %[ti32], %[wri], %[frfi_r], %[cfftrnd]\n\t"
                        :[frfi_r]"=&r"(frfi_r),
                         [tr32]"=&r"(tr32),
                         [ti32]"=r"(ti32)
                        :[frfi_even]"r"((int32_t)frfi[2*j]),
                         [frfi_odd]"r"((int32_t)frfi[2*j +1]),
                         [wri]"r"(wri),
                         [cfftrnd]"r"(CFFTRND));
#else
                    tr32 = wr * frfi[2 * j] - wi * frfi[2 * j + 1] + CFFTRND;

                    ti32 = wr * frfi[2 * j + 1] + wi * frfi[2 * j] + CFFTRND;
#endif

                    tr32 >>= 15 - CFFTSFT;
                    ti32 >>= 15 - CFFTSFT;

                    qr32 = ((int32_t) frfi[2 * i]) * (1 << CFFTSFT);
                    qi32 = ((int32_t) frfi[2 * i + 1]) * (1 << CFFTSFT);

                    frfi[2 * j] = (int16_t) (
                            (qr32 - tr32 + CFFTRND2) >> (1 + CFFTSFT));
                    frfi[2 * j + 1] = (int16_t) (
                            (qi32 - ti32 + CFFTRND2) >> (1 + CFFTSFT));
                    frfi[2 * i] = (int16_t) (
                            (qr32 + tr32 + CFFTRND2) >> (1 + CFFTSFT));
                    frfi[2 * i + 1] = (int16_t) (
                            (qi32 + ti32 + CFFTRND2) >> (1 + CFFTSFT));
                }
            }

            --k;
            l = istep;
        }
    }
    return 0;
}

int16_t WebRtcSpl_MaxAbsValueW16C(const int16_t *vector, size_t length) {
    size_t i = 0;
    int absolute = 0, maximum = 0;

    for (i = 0; i < length; i++) {
        absolute = abs((int) vector[i]);

        if (absolute > maximum) {
            maximum = absolute;
        }
    }

    // Guard the case for abs(-32768).
    if (maximum > 32767) {
        maximum = 32767;
    }

    return (int16_t) maximum;
}

int WebRtcSpl_ComplexIFFT(int16_t frfi[], int stages, int mode) {
    size_t i, j, l, istep, n, m;
    int k, scale, shift;
    int16_t wr, wi;
    int32_t tr32, ti32, qr32, qi32;
    int32_t tmp32, round2;

    /* The 1024-value is a constant given from the size of kSinTable1024[],
     * and should not be changed depending on the input parameter 'stages'
     */
    n = 1 << stages;
    if (n > 1024)
        return -1;

    scale = 0;

    l = 1;
    k = 10 - 1; /* Constant for given kSinTable1024[]. Do not change
         depending on the input parameter 'stages' */

    while (l < n) {
        // variable scaling, depending upon data
        shift = 0;
        round2 = 8192;

        tmp32 = WebRtcSpl_MaxAbsValueW16C(frfi, 2 * n);
        if (tmp32 > 13573) {
            shift++;
            scale++;
            round2 <<= 1;
        }
        if (tmp32 > 27146) {
            shift++;
            scale++;
            round2 <<= 1;
        }

        istep = l << 1;

        if (mode == 0) {
            // mode==0: Low-complexity and Low-accuracy mode
            for (m = 0; m < l; ++m) {
                j = m << k;

                /* The 256-value is a constant given as 1/4 of the size of
                 * kSinTable1024[], and should not be changed depending on the input
                 * parameter 'stages'. It will result in 0 <= j < N_SINE_WAVE/2
                 */
                wr = kSinTable1024[j + 256];
                wi = kSinTable1024[j];

                for (i = m; i < n; i += istep) {
                    j = i + l;

                    tr32 = (wr * frfi[2 * j] - wi * frfi[2 * j + 1]) >> 15;

                    ti32 = (wr * frfi[2 * j + 1] + wi * frfi[2 * j]) >> 15;

                    qr32 = (int32_t) frfi[2 * i];
                    qi32 = (int32_t) frfi[2 * i + 1];
                    frfi[2 * j] = (int16_t) ((qr32 - tr32) >> shift);
                    frfi[2 * j + 1] = (int16_t) ((qi32 - ti32) >> shift);
                    frfi[2 * i] = (int16_t) ((qr32 + tr32) >> shift);
                    frfi[2 * i + 1] = (int16_t) ((qi32 + ti32) >> shift);
                }
            }
        } else {
            // mode==1: High-complexity and High-accuracy mode

            for (m = 0; m < l; ++m) {
                j = m << k;

                /* The 256-value is a constant given as 1/4 of the size of
                 * kSinTable1024[], and should not be changed depending on the input
                 * parameter 'stages'. It will result in 0 <= j < N_SINE_WAVE/2
                 */
                wr = kSinTable1024[j + 256];
                wi = kSinTable1024[j];

#ifdef WEBRTC_ARCH_ARM_V7
                int32_t wri = 0;
                __asm __volatile("pkhbt %0, %1, %2, lsl #16" : "=r"(wri) :
                    "r"((int32_t)wr), "r"((int32_t)wi));
#endif

                for (i = m; i < n; i += istep) {
                    j = i + l;

#ifdef WEBRTC_ARCH_ARM_V7
                    register int32_t frfi_r;
                    __asm __volatile(
                      "pkhbt %[frfi_r], %[frfi_even], %[frfi_odd], lsl #16\n\t"
                      "smlsd %[tr32], %[wri], %[frfi_r], %[cifftrnd]\n\t"
                      "smladx %[ti32], %[wri], %[frfi_r], %[cifftrnd]\n\t"
                      :[frfi_r]"=&r"(frfi_r),
                       [tr32]"=&r"(tr32),
                       [ti32]"=r"(ti32)
                      :[frfi_even]"r"((int32_t)frfi[2*j]),
                       [frfi_odd]"r"((int32_t)frfi[2*j +1]),
                       [wri]"r"(wri),
                       [cifftrnd]"r"(CIFFTRND)
                    );
#else

                    tr32 = wr * frfi[2 * j] - wi * frfi[2 * j + 1] + CIFFTRND;

                    ti32 = wr * frfi[2 * j + 1] + wi * frfi[2 * j] + CIFFTRND;
#endif
                    tr32 >>= 15 - CIFFTSFT;
                    ti32 >>= 15 - CIFFTSFT;

                    qr32 = ((int32_t) frfi[2 * i]) * (1 << CIFFTSFT);
                    qi32 = ((int32_t) frfi[2 * i + 1]) * (1 << CIFFTSFT);

                    frfi[2 * j] = (int16_t) (
                            (qr32 - tr32 + round2) >> (shift + CIFFTSFT));
                    frfi[2 * j + 1] = (int16_t) (
                            (qi32 - ti32 + round2) >> (shift + CIFFTSFT));
                    frfi[2 * i] = (int16_t) (
                            (qr32 + tr32 + round2) >> (shift + CIFFTSFT));
                    frfi[2 * i + 1] = (int16_t) (
                            (qi32 + ti32 + round2) >> (shift + CIFFTSFT));
                }
            }

        }
        --k;
        l = istep;
    }
    return scale;
}

struct RealFFT {
    int order;
};

struct RealFFT *WebRtcSpl_CreateRealFFT(int order) {
    struct RealFFT *self = NULL;

    if (order > kMaxFFTOrder || order < 0) {
        return NULL;
    }

    self = malloc(sizeof(struct RealFFT));
    if (self == NULL) {
        return NULL;
    }
    self->order = order;

    return self;
}

void WebRtcSpl_FreeRealFFT(struct RealFFT *self) {
    if (self != NULL) {
        free(self);
    }
}

// The C version FFT functions (i.e. WebRtcSpl_RealForwardFFT and
// WebRtcSpl_RealInverseFFT) are real-valued FFT wrappers for complex-valued
// FFT implementation in SPL.

int WebRtcSpl_RealForwardFFT(struct RealFFT *self,
                             const int16_t *real_data_in,
                             int16_t *complex_data_out) {
    int i = 0;
    int j = 0;
    int result = 0;
    int n = 1 << self->order;
    // The complex-value FFT implementation needs a buffer to hold 2^order
    // 16-bit COMPLEX numbers, for both time and frequency data.
    int16_t complex_buffer[2 << kMaxFFTOrder];

    // Insert zeros to the imaginary parts for complex forward FFT input.
    for (i = 0, j = 0; i < n; i += 1, j += 2) {
        complex_buffer[j] = real_data_in[i];
        complex_buffer[j + 1] = 0;
    };

    WebRtcSpl_ComplexBitReverse(complex_buffer, self->order);
    result = WebRtcSpl_ComplexFFT(complex_buffer, self->order, 1);

    // For real FFT output, use only the first N + 2 elements from
    // complex forward FFT.
    memcpy(complex_data_out, complex_buffer, sizeof(int16_t) * (n + 2));

    return result;
}

int WebRtcSpl_RealInverseFFT(struct RealFFT *self,
                             const int16_t *complex_data_in,
                             int16_t *real_data_out) {
    int i = 0;
    int j = 0;
    int result = 0;
    int n = 1 << self->order;
    // Create the buffer specific to complex-valued FFT implementation.
    int16_t complex_buffer[2 << kMaxFFTOrder];

    // For n-point FFT, first copy the first n + 2 elements into complex
    // FFT, then construct the remaining n - 2 elements by real FFT's
    // conjugate-symmetric properties.
    memcpy(complex_buffer, complex_data_in, sizeof(int16_t) * (n + 2));
    for (i = n + 2; i < 2 * n; i += 2) {
        complex_buffer[i] = complex_data_in[2 * n - i];
        complex_buffer[i + 1] = -complex_data_in[2 * n - i + 1];
    }

    WebRtcSpl_ComplexBitReverse(complex_buffer, self->order);
    result = WebRtcSpl_ComplexIFFT(complex_buffer, self->order, 1);

    // Strip out the imaginary parts of the complex inverse FFT output.
    for (i = 0, j = 0; i < n; i += 1, j += 2) {
        real_data_out[i] = complex_buffer[j];
    }

    return result;
}

// Estimates delay to set the position of the farend buffer read pointer
// (controlled by knownDelay)
static int WebRtcAecm_EstBufDelay(AecMobile *aecm, short msInSndCardBuf);

// Stuffs the farend buffer if the estimated delay is too large
static int WebRtcAecm_DelayComp(AecMobile *aecm);

void *WebRtcAecm_Create() {
    AecMobile *aecm = (AecMobile *) (malloc(sizeof(AecMobile)));


    aecm->aecmCore = WebRtcAecm_CreateCore();
    if (!aecm->aecmCore) {
        WebRtcAecm_Free(aecm);
        return NULL;
    }

    aecm->farendBuf = WebRtc_CreateBuffer(kBufSizeSamp,
                                          sizeof(int16_t));
    if (!aecm->farendBuf) {
        WebRtcAecm_Free(aecm);
        return NULL;
    }

    aecm->initFlag = 0;

#ifdef AEC_DEBUG
    aecm->aecmCore->farFile = fopen("aecFar.pcm","wb");
    aecm->aecmCore->nearFile = fopen("aecNear.pcm","wb");
    aecm->aecmCore->outFile = fopen("aecOut.pcm","wb");
    //aecm->aecmCore->outLpFile = fopen("aecOutLp.pcm","wb");

    aecm->bufFile = fopen("aecBuf.dat", "wb");
    aecm->delayFile = fopen("aecDelay.dat", "wb");
    aecm->preCompFile = fopen("preComp.pcm", "wb");
    aecm->postCompFile = fopen("postComp.pcm", "wb");
#endif // AEC_DEBUG
    return aecm;
}

void WebRtcAecm_Free(void *aecmInst) {
    AecMobile *aecm = (AecMobile *) (aecmInst);

    if (aecm == NULL) {
        return;
    }

#ifdef AEC_DEBUG
    fclose(aecm->aecmCore->farFile);
    fclose(aecm->aecmCore->nearFile);
    fclose(aecm->aecmCore->outFile);
    //fclose(aecm->aecmCore->outLpFile);

    fclose(aecm->bufFile);
    fclose(aecm->delayFile);
    fclose(aecm->preCompFile);
    fclose(aecm->postCompFile);
#endif // AEC_DEBUG
    WebRtcAecm_FreeCore(aecm->aecmCore);
    WebRtc_FreeBuffer(aecm->farendBuf);
    free(aecm);
}

int32_t WebRtcAecm_Init(void *aecmInst, int32_t sampFreq) {
    AecMobile *aecm = (AecMobile *) (aecmInst);
    AecmConfig aecConfig;

    if (aecm == NULL) {
        return -1;
    }

    if (sampFreq != 8000 && sampFreq != 16000) {
        return AECM_BAD_PARAMETER_ERROR;
    }
    aecm->sampFreq = sampFreq;

    // Initialize AECM core
    if (WebRtcAecm_InitCore(aecm->aecmCore, aecm->sampFreq) == -1) {
        return AECM_UNSPECIFIED_ERROR;
    }

    // Initialize farend buffer
    WebRtc_InitBuffer(aecm->farendBuf);

    aecm->initFlag = kInitCheck; // indicates that initialization has been done

    aecm->delayChange = 1;

    aecm->sum = 0;
    aecm->counter = 0;
    aecm->checkBuffSize = 1;
    aecm->firstVal = 0;

    aecm->ECstartup = 1;
    aecm->bufSizeStart = 0;
    aecm->checkBufSizeCtr = 0;
    aecm->filtDelay = 0;
    aecm->timeForDelayChange = 0;
    aecm->knownDelay = 0;
    aecm->lastDelayDiff = 0;

    memset(&aecm->farendOld, 0, sizeof(aecm->farendOld));

    // Default settings.
    aecConfig.cngMode = AecmTrue;
    aecConfig.echoMode = 3;

    if (WebRtcAecm_set_config(aecm, aecConfig) == -1) {
        return AECM_UNSPECIFIED_ERROR;
    }

    return 0;
}

// Returns any error that is caused when buffering the
// farend signal.
int32_t WebRtcAecm_GetBufferFarendError(void *aecmInst, const int16_t *farend,
                                        size_t nrOfSamples) {
    AecMobile *aecm = (AecMobile *) (aecmInst);

    if (aecm == NULL)
        return -1;

    if (farend == NULL)
        return AECM_NULL_POINTER_ERROR;

    if (aecm->initFlag != kInitCheck)
        return AECM_UNINITIALIZED_ERROR;

    if (nrOfSamples != 80 && nrOfSamples != 160)
        return AECM_BAD_PARAMETER_ERROR;

    return 0;
}


int32_t WebRtcAecm_BufferFarend(void *aecmInst, const int16_t *farend,
                                size_t nrOfSamples) {
    AecMobile *aecm = (AecMobile *) (aecmInst);

    const int32_t err =
            WebRtcAecm_GetBufferFarendError(aecmInst, farend, nrOfSamples);

    if (err != 0)
        return err;

    // TODO(unknown): Is this really a good idea?
    if (!aecm->ECstartup) {
        WebRtcAecm_DelayComp(aecm);
    }

    WebRtc_WriteBuffer(aecm->farendBuf, farend, nrOfSamples);

    return 0;
}

int32_t WebRtcAecm_Process(void *aecmInst, const int16_t *nearendNoisy,
                           const int16_t *nearendClean, int16_t *out,
                           size_t nrOfSamples, int16_t msInSndCardBuf) {
    AecMobile *aecm = (AecMobile *) (aecmInst);
    int32_t retVal = 0;
    size_t i;
    short nmbrOfFilledBuffers;
    size_t nBlocks10ms;
    size_t nFrames;
#ifdef AEC_DEBUG
    short msInAECBuf;
#endif

    if (aecm == NULL) {
        return -1;
    }

    if (nearendNoisy == NULL) {
        return AECM_NULL_POINTER_ERROR;
    }

    if (out == NULL) {
        return AECM_NULL_POINTER_ERROR;
    }

    if (aecm->initFlag != kInitCheck) {
        return AECM_UNINITIALIZED_ERROR;
    }

    if (nrOfSamples != 80 && nrOfSamples != 160) {
        return AECM_BAD_PARAMETER_ERROR;
    }

    if (msInSndCardBuf < 0) {
        msInSndCardBuf = 0;
        retVal = AECM_BAD_PARAMETER_WARNING;
    } else if (msInSndCardBuf > 500) {
        msInSndCardBuf = 500;
        retVal = AECM_BAD_PARAMETER_WARNING;
    }
    msInSndCardBuf += 10;
    aecm->msInSndCardBuf = msInSndCardBuf;

    nFrames = nrOfSamples / FRAME_LEN;
    nBlocks10ms = nFrames / aecm->aecmCore->mult;

    if (aecm->ECstartup) {
        if (nearendClean == NULL) {
            if (out != nearendNoisy) {
                memcpy(out, nearendNoisy, sizeof(short) * nrOfSamples);
            }
        } else if (out != nearendClean) {
            memcpy(out, nearendClean, sizeof(short) * nrOfSamples);
        }

        nmbrOfFilledBuffers =
                (short) WebRtc_available_read(aecm->farendBuf) / FRAME_LEN;
        // The AECM is in the start up mode
        // AECM is disabled until the soundcard buffer and farend buffers are OK

        // Mechanism to ensure that the soundcard buffer is reasonably stable.
        if (aecm->checkBuffSize) {
            aecm->checkBufSizeCtr++;
            // Before we fill up the far end buffer we require the amount of data on the
            // sound card to be stable (+/-8 ms) compared to the first value. This
            // comparison is made during the following 4 consecutive frames. If it seems
            // to be stable then we start to fill up the far end buffer.

            if (aecm->counter == 0) {
                aecm->firstVal = aecm->msInSndCardBuf;
                aecm->sum = 0;
            }

            if (abs(aecm->firstVal - aecm->msInSndCardBuf)
                < WEBRTC_SPL_MAX(0.2 * aecm->msInSndCardBuf, kSampMsNb)) {
                aecm->sum += aecm->msInSndCardBuf;
                aecm->counter++;
            } else {
                aecm->counter = 0;
            }

            if (aecm->counter * nBlocks10ms >= 6) {
                // The farend buffer size is determined in blocks of 80 samples
                // Use 75% of the average value of the soundcard buffer
                aecm->bufSizeStart
                        = WEBRTC_SPL_MIN((3 * aecm->sum
                                          * aecm->aecmCore->mult) / (aecm->counter * 40), BUF_SIZE_FRAMES);
                // buffersize has now been determined
                aecm->checkBuffSize = 0;
            }

            if (aecm->checkBufSizeCtr * nBlocks10ms > 50) {
                // for really bad sound cards, don't disable echocanceller for more than 0.5 sec
                aecm->bufSizeStart = WEBRTC_SPL_MIN((3 * aecm->msInSndCardBuf
                                                     * aecm->aecmCore->mult) / 40, BUF_SIZE_FRAMES);
                aecm->checkBuffSize = 0;
            }
        }

        // if checkBuffSize changed in the if-statement above
        if (!aecm->checkBuffSize) {
            // soundcard buffer is now reasonably stable
            // When the far end buffer is filled with approximately the same amount of
            // data as the amount on the sound card we end the start up phase and start
            // to cancel echoes.

            if (nmbrOfFilledBuffers == aecm->bufSizeStart) {
                aecm->ECstartup = 0; // Enable the AECM
            } else if (nmbrOfFilledBuffers > aecm->bufSizeStart) {
                WebRtc_MoveReadPtr(aecm->farendBuf,
                                   (int) WebRtc_available_read(aecm->farendBuf)
                                   - (int) aecm->bufSizeStart * FRAME_LEN);
                aecm->ECstartup = 0;
            }
        }

    } else {
        // AECM is enabled

        // Note only 1 block supported for nb and 2 blocks for wb
        for (i = 0; i < nFrames; i++) {
            int16_t farend[FRAME_LEN];
            const int16_t *farend_ptr = NULL;

            nmbrOfFilledBuffers =
                    (short) WebRtc_available_read(aecm->farendBuf) / FRAME_LEN;

            // Check that there is data in the far end buffer
            if (nmbrOfFilledBuffers > 0) {
                // Get the next 80 samples from the farend buffer
                WebRtc_ReadBuffer(aecm->farendBuf, (void **) &farend_ptr, farend,
                                  FRAME_LEN);

                // Always store the last frame for use when we run out of data
                memcpy(&(aecm->farendOld[i][0]), farend_ptr,
                       FRAME_LEN * sizeof(short));
            } else {
                // We have no data so we use the last played frame
                memcpy(farend, &(aecm->farendOld[i][0]), FRAME_LEN * sizeof(short));
                farend_ptr = farend;
            }

            // Call buffer delay estimator when all data is extracted,
            // i,e. i = 0 for NB and i = 1 for WB
            if ((i == 0 && aecm->sampFreq == 8000) || (i == 1 && aecm->sampFreq == 16000)) {
                WebRtcAecm_EstBufDelay(aecm, aecm->msInSndCardBuf);
            }

            // Call the AECM
            /*WebRtcAecm_ProcessFrame(aecm->aecmCore, farend, &nearend[FRAME_LEN * i],
             &out[FRAME_LEN * i], aecm->knownDelay);*/
            if (WebRtcAecm_ProcessFrame(aecm->aecmCore,
                                        farend_ptr,
                                        &nearendNoisy[FRAME_LEN * i],
                                        (nearendClean
                                         ? &nearendClean[FRAME_LEN * i]
                                         : NULL),
                                        &out[FRAME_LEN * i]) == -1)
                return -1;
        }
    }

#ifdef AEC_DEBUG
    msInAECBuf = (short) WebRtc_available_read(aecm->farendBuf) /
        (kSampMsNb * aecm->aecmCore->mult);
    fwrite(&msInAECBuf, 2, 1, aecm->bufFile);
    fwrite(&(aecm->knownDelay), sizeof(aecm->knownDelay), 1, aecm->delayFile);
#endif

    return retVal;
}

int32_t WebRtcAecm_set_config(void *aecmInst, AecmConfig config) {
    AecMobile *aecm = (AecMobile *) (aecmInst);

    if (aecm == NULL) {
        return -1;
    }

    if (aecm->initFlag != kInitCheck) {
        return AECM_UNINITIALIZED_ERROR;
    }

    if (config.cngMode != AecmFalse && config.cngMode != AecmTrue) {
        return AECM_BAD_PARAMETER_ERROR;
    }
    aecm->aecmCore->cngMode = config.cngMode;

    if (config.echoMode < 0 || config.echoMode > 4) {
        return AECM_BAD_PARAMETER_ERROR;
    }
    aecm->echoMode = config.echoMode;

    if (aecm->echoMode == 0) {
        aecm->aecmCore->supGain = SUPGAIN_DEFAULT >> 3;
        aecm->aecmCore->supGainOld = SUPGAIN_DEFAULT >> 3;
        aecm->aecmCore->supGainErrParamA = SUPGAIN_ERROR_PARAM_A >> 3;
        aecm->aecmCore->supGainErrParamD = SUPGAIN_ERROR_PARAM_D >> 3;
        aecm->aecmCore->supGainErrParamDiffAB = (SUPGAIN_ERROR_PARAM_A >> 3)
                                                - (SUPGAIN_ERROR_PARAM_B >> 3);
        aecm->aecmCore->supGainErrParamDiffBD = (SUPGAIN_ERROR_PARAM_B >> 3)
                                                - (SUPGAIN_ERROR_PARAM_D >> 3);
    } else if (aecm->echoMode == 1) {
        aecm->aecmCore->supGain = SUPGAIN_DEFAULT >> 2;
        aecm->aecmCore->supGainOld = SUPGAIN_DEFAULT >> 2;
        aecm->aecmCore->supGainErrParamA = SUPGAIN_ERROR_PARAM_A >> 2;
        aecm->aecmCore->supGainErrParamD = SUPGAIN_ERROR_PARAM_D >> 2;
        aecm->aecmCore->supGainErrParamDiffAB = (SUPGAIN_ERROR_PARAM_A >> 2)
                                                - (SUPGAIN_ERROR_PARAM_B >> 2);
        aecm->aecmCore->supGainErrParamDiffBD = (SUPGAIN_ERROR_PARAM_B >> 2)
                                                - (SUPGAIN_ERROR_PARAM_D >> 2);
    } else if (aecm->echoMode == 2) {
        aecm->aecmCore->supGain = SUPGAIN_DEFAULT >> 1;
        aecm->aecmCore->supGainOld = SUPGAIN_DEFAULT >> 1;
        aecm->aecmCore->supGainErrParamA = SUPGAIN_ERROR_PARAM_A >> 1;
        aecm->aecmCore->supGainErrParamD = SUPGAIN_ERROR_PARAM_D >> 1;
        aecm->aecmCore->supGainErrParamDiffAB = (SUPGAIN_ERROR_PARAM_A >> 1)
                                                - (SUPGAIN_ERROR_PARAM_B >> 1);
        aecm->aecmCore->supGainErrParamDiffBD = (SUPGAIN_ERROR_PARAM_B >> 1)
                                                - (SUPGAIN_ERROR_PARAM_D >> 1);
    } else if (aecm->echoMode == 3) {
        aecm->aecmCore->supGain = SUPGAIN_DEFAULT;
        aecm->aecmCore->supGainOld = SUPGAIN_DEFAULT;
        aecm->aecmCore->supGainErrParamA = SUPGAIN_ERROR_PARAM_A;
        aecm->aecmCore->supGainErrParamD = SUPGAIN_ERROR_PARAM_D;
        aecm->aecmCore->supGainErrParamDiffAB = SUPGAIN_ERROR_PARAM_A - SUPGAIN_ERROR_PARAM_B;
        aecm->aecmCore->supGainErrParamDiffBD = SUPGAIN_ERROR_PARAM_B - SUPGAIN_ERROR_PARAM_D;
    } else if (aecm->echoMode == 4) {
        aecm->aecmCore->supGain = SUPGAIN_DEFAULT << 1;
        aecm->aecmCore->supGainOld = SUPGAIN_DEFAULT << 1;
        aecm->aecmCore->supGainErrParamA = SUPGAIN_ERROR_PARAM_A << 1;
        aecm->aecmCore->supGainErrParamD = SUPGAIN_ERROR_PARAM_D << 1;
        aecm->aecmCore->supGainErrParamDiffAB = (SUPGAIN_ERROR_PARAM_A << 1)
                                                - (SUPGAIN_ERROR_PARAM_B << 1);
        aecm->aecmCore->supGainErrParamDiffBD = (SUPGAIN_ERROR_PARAM_B << 1)
                                                - (SUPGAIN_ERROR_PARAM_D << 1);
    }

    return 0;
}

int32_t WebRtcAecm_InitEchoPath(void *aecmInst,
                                const void *echo_path,
                                size_t size_bytes) {
    AecMobile *aecm = (AecMobile *) (aecmInst);
    const int16_t *echo_path_ptr = (const int16_t *) (echo_path);

    if (aecmInst == NULL) {
        return -1;
    }
    if (echo_path == NULL) {
        return AECM_NULL_POINTER_ERROR;
    }
    if (size_bytes != WebRtcAecm_echo_path_size_bytes()) {
        // Input channel size does not match the size of AECM
        return AECM_BAD_PARAMETER_ERROR;
    }
    if (aecm->initFlag != kInitCheck) {
        return AECM_UNINITIALIZED_ERROR;
    }

    WebRtcAecm_InitEchoPathCore(aecm->aecmCore, echo_path_ptr);

    return 0;
}

int32_t WebRtcAecm_GetEchoPath(void *aecmInst,
                               void *echo_path,
                               size_t size_bytes) {
    AecMobile *aecm = (AecMobile *) (aecmInst);
    int16_t *echo_path_ptr = (int16_t *) (echo_path);

    if (aecmInst == NULL) {
        return -1;
    }
    if (echo_path == NULL) {
        return AECM_NULL_POINTER_ERROR;
    }
    if (size_bytes != WebRtcAecm_echo_path_size_bytes()) {
        // Input channel size does not match the size of AECM
        return AECM_BAD_PARAMETER_ERROR;
    }
    if (aecm->initFlag != kInitCheck) {
        return AECM_UNINITIALIZED_ERROR;
    }

    memcpy(echo_path_ptr, aecm->aecmCore->channelStored, size_bytes);
    return 0;
}

size_t WebRtcAecm_echo_path_size_bytes() {
    return (PART_LEN1 * sizeof(int16_t));
}


static int WebRtcAecm_EstBufDelay(AecMobile *aecm, short msInSndCardBuf) {
    short delayNew, nSampSndCard;
    short nSampFar = (short) WebRtc_available_read(aecm->farendBuf);
    short diff;

    nSampSndCard = msInSndCardBuf * kSampMsNb * aecm->aecmCore->mult;

    delayNew = nSampSndCard - nSampFar;

    if (delayNew < FRAME_LEN) {
        WebRtc_MoveReadPtr(aecm->farendBuf, FRAME_LEN);
        delayNew += FRAME_LEN;
    }

    aecm->filtDelay = WEBRTC_SPL_MAX(0, (8 * aecm->filtDelay + 2 * delayNew) / 10);

    diff = aecm->filtDelay - aecm->knownDelay;
    if (diff > 224) {
        if (aecm->lastDelayDiff < 96) {
            aecm->timeForDelayChange = 0;
        } else {
            aecm->timeForDelayChange++;
        }
    } else if (diff < 96 && aecm->knownDelay > 0) {
        if (aecm->lastDelayDiff > 224) {
            aecm->timeForDelayChange = 0;
        } else {
            aecm->timeForDelayChange++;
        }
    } else {
        aecm->timeForDelayChange = 0;
    }
    aecm->lastDelayDiff = diff;

    if (aecm->timeForDelayChange > 25) {
        aecm->knownDelay = WEBRTC_SPL_MAX((int) aecm->filtDelay - 160, 0);
    }
    return 0;
}

static int WebRtcAecm_DelayComp(AecMobile *aecm) {
    int nSampFar = (int) WebRtc_available_read(aecm->farendBuf);
    int nSampSndCard, delayNew, nSampAdd;
    const int maxStuffSamp = 10 * FRAME_LEN;

    nSampSndCard = aecm->msInSndCardBuf * kSampMsNb * aecm->aecmCore->mult;
    delayNew = nSampSndCard - nSampFar;

    if (delayNew > FAR_BUF_LEN - FRAME_LEN * aecm->aecmCore->mult) {
        // The difference of the buffer sizes is larger than the maximum
        // allowed known delay. Compensate by stuffing the buffer.
        nSampAdd = (WEBRTC_SPL_MAX(((nSampSndCard >> 1) - nSampFar),
                                   FRAME_LEN));
        nSampAdd = WEBRTC_SPL_MIN(nSampAdd, maxStuffSamp);

        WebRtc_MoveReadPtr(aecm->farendBuf, -nSampAdd);
        aecm->delayChange = 1; // the delay needs to be updated
    }

    return 0;
}
