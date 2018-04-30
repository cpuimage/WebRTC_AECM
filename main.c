#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
//采用https://github.com/mackron/dr_libs/blob/master/dr_wav.h 解码
#define DR_WAV_IMPLEMENTATION

#include "dr_wav.h"
#include "aecm.h"
#include "timing.h"

#ifndef nullptr
#define nullptr 0
#endif

#ifndef MIN
#define  MIN(A, B)        ((A) < (B) ? (A) : (B))
#endif

//写wav文件
void wavWrite_int16(char *filename, int16_t *buffer, size_t sampleRate, size_t totalSampleCount) {
    drwav_data_format format = {};
    format.container = drwav_container_riff;     // <-- drwav_container_riff = normal WAV files, drwav_container_w64 = Sony Wave64.
    format.format = DR_WAVE_FORMAT_PCM;          // <-- Any of the DR_WAVE_FORMAT_* codes.
    format.channels = 1;
    format.sampleRate = (drwav_uint32) sampleRate;
    format.bitsPerSample = 16;
    drwav *pWav = drwav_open_file_write(filename, &format);
    if (pWav) {
        drwav_uint64 samplesWritten = drwav_write(pWav, totalSampleCount, buffer);
        drwav_uninit(pWav);
        if (samplesWritten != totalSampleCount) {
            fprintf(stderr, "ERROR\n");
            exit(1);
        }
    }
}

//读取wav文件
int16_t *wavRead_int16(char *filename, uint32_t *sampleRate, uint64_t *totalSampleCount) {
    unsigned int channels;
    int16_t *buffer = drwav_open_and_read_file_s16(filename, &channels, sampleRate, totalSampleCount);
    if (buffer == nullptr) {
        printf("读取wav文件失败.");
    }
    //仅仅处理单通道音频
    if (channels != 1) {
        drwav_free(buffer);
        buffer = nullptr;
        *sampleRate = 0;
        *totalSampleCount = 0;
    }
    return buffer;
}

//分割路径函数
void splitpath(const char *path, char *drv, char *dir, char *name, char *ext) {
    const char *end;
    const char *p;
    const char *s;
    if (path[0] && path[1] == ':') {
        if (drv) {
            *drv++ = *path++;
            *drv++ = *path++;
            *drv = '\0';
        }
    } else if (drv)
        *drv = '\0';
    for (end = path; *end && *end != ':';)
        end++;
    for (p = end; p > path && *--p != '\\' && *p != '/';)
        if (*p == '.') {
            end = p;
            break;
        }
    if (ext)
        for (s = end; (*ext = *s++);)
            ext++;
    for (p = end; p > path;)
        if (*--p == '\\' || *p == '/') {
            p++;
            break;
        }
    if (name) {
        for (s = p; s < end;)
            *name++ = *s++;
        *name = '\0';
    }
    if (dir) {
        for (s = path; s < p;)
            *dir++ = *s++;
        *dir = '\0';
    }
}


int aecProcess(int16_t *far_frame, int16_t *near_frame, uint32_t sampleRate, size_t samplesCount, int16_t nMode,
               int16_t msInSndCardBuf) {
    if (near_frame == nullptr) return -1;
    if (far_frame == nullptr) return -1;
    if (samplesCount == 0) return -1;
    AecmConfig config;
    config.cngMode = AecmTrue;
    config.echoMode = nMode;// 0, 1, 2, 3 (default), 4
    size_t samples = MIN(160, sampleRate / 100);
    if (samples == 0) return -1;
    const int maxSamples = 160;
    int16_t *near_input = near_frame;
    int16_t *far_input = far_frame;
    size_t nTotal = (samplesCount / samples);
    void *aecmInst = WebRtcAecm_Create();
    if (aecmInst == NULL) return -1;
    int status = WebRtcAecm_Init(aecmInst, sampleRate);//8000 or 16000 Sample rate
    if (status != 0) {
        printf("WebRtcAecm_Init fail\n");
        WebRtcAecm_Free(aecmInst);
        return -1;
    }
    status = WebRtcAecm_set_config(aecmInst, config);
    if (status != 0) {
        printf("WebRtcAecm_set_config fail\n");
        WebRtcAecm_Free(aecmInst);
        return -1;
    }

    int16_t out_buffer[maxSamples];
    for (int i = 0; i < nTotal; i++) {
        if (WebRtcAecm_BufferFarend(aecmInst, far_input, samples) != 0) {
            printf("WebRtcAecm_BufferFarend() failed.");
            WebRtcAecm_Free(aecmInst);
            return -1;
        }
        int nRet = WebRtcAecm_Process(aecmInst, near_input, NULL, out_buffer, samples, msInSndCardBuf);

        if (nRet != 0) {
            printf("failed in WebRtcAecm_Process\n");
            WebRtcAecm_Free(aecmInst);
            return -1;
        }
        memcpy(near_input, out_buffer, samples * sizeof(int16_t));
        near_input += samples;
        far_input += samples;
    }
    WebRtcAecm_Free(aecmInst);
    return 1;
}

void AECM(char *near_file, char *far_file, char *out_file) {
    //音频采样率
    uint32_t sampleRate = 0;
    uint64_t inSampleCount = 0;
    uint32_t ref_sampleRate = 0;
    uint64_t ref_inSampleCount = 0;
    int16_t *near_frame = wavRead_int16(near_file, &sampleRate, &inSampleCount);
    int16_t *far_frame = wavRead_int16(far_file, &ref_sampleRate, &ref_inSampleCount);
    if ((near_frame == nullptr || far_frame == nullptr)) {
        if (near_frame) free(near_frame);
        if (far_frame) free(far_frame);
        return;
    }
    //如果加载成功
    int16_t echoMode = 1;// 0, 1, 2, 3 (default), 4
    int16_t msInSndCardBuf = 40;
    double startTime = now();
    aecProcess(far_frame, near_frame, sampleRate, inSampleCount, echoMode, msInSndCardBuf);
    double elapsed_time = calcElapsed(startTime, now());
    printf("time interval: %d ms\n ", (int) (elapsed_time * 1000));
    wavWrite_int16(out_file, near_frame, sampleRate, inSampleCount);
    free(near_frame);
    free(far_frame);
}

int main(int argc, char *argv[]) {
    printf("WebRTC Acoustic Echo Canceller for Mobile\n");
    printf("blog:http://cpuimage.cnblogs.com/\n");
    printf("usage : aecm far_file.wav near_file.wav\n");
    if (argc < 3)
        return -1;
    // echo file
    char *far_file = argv[1];
    // mixed file
    char *near_file = argv[2];
    char drive[3];
    char dir[256];
    char fname[256];
    char ext[256];
    char out_file[1024];
    splitpath(near_file, drive, dir, fname, ext);
    sprintf(out_file, "%s%s%s_out%s", drive, dir, fname, ext);
    AECM(near_file, far_file, out_file);
    printf("press any key to exit. \n");
    getchar();
    return 0;
}
