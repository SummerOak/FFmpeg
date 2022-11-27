/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2009 Giliard B. de Freitas <giliarde@gmail.com>
 * Copyright (C) 2002 Gunnar Monell <gmo@linux.nu>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Linux framebuffer input device,
 * inspired by code from fbgrab.c by Gunnar Monell.
 * @see http://linux-fbdev.sourceforge.net/
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>

#define _GNU_SOURCE
#include <sys/shm.h>
#include <sys/sem.h>

#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavformat/internal.h"
#include "avdevice.h"

union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO (Linux-specific) */
};

static int sem_init(int semid) {
    union semun sem_union;
    sem_union.val = 0;
    return semctl(semid, 0, SETVAL, sem_union);
}

int  semtimedop(int  semid, struct sembuf *sops, unsigned nsops, struct timespec *timeout);
static int P(int semid, const struct timespec *timeout) {
    struct sembuf sops = {0, -1, 0};
    return semtimedop(semid, &sops, 1, timeout);
}

typedef struct SHMEMDevContext {
    AVClass *class;          ///< class for private options
    const char *fifo;
    AVRational minfps;
    int width, height;       ///< assumed frame resolution
    const enum AVPixelFormat pixelFmt;

    int frame_size;          ///< size in bytes of a grabbed frame
    int semid;
    struct timespec timeout;
    uint8_t *data;           ///< framebuffer data
} SHMEMDevContext;

static av_cold int fbdev_read_header(AVFormatContext *avctx)
{
    int ret;
    SHMEMDevContext *fbdev = avctx->priv_data;

    av_log(avctx, AV_LOG_ERROR, "init shmem dev with width=%d, height=%d, pixelFmt=%d, minfps=%d (den=%d num=%d) fifo=%s\n", 
        fbdev->width, fbdev->height, fbdev->pixelFmt, fbdev->minfps, fbdev->minfps.den, fbdev->minfps.num, fbdev->fifo);

    key_t key = ftok(fbdev->fifo, 65);
    if (key == -1) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR, "ftok %s failed: %s\n", fbdev->fifo, av_err2str(ret));
        return ret;
    }

    // shmget returns an identifier in shmid
    fbdev->frame_size = (fbdev->width * fbdev->height*3)/2;
    int shmid = shmget(key, fbdev->frame_size+1024, 0666|IPC_CREAT);
    if (shmid == -1) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR, "shmget failed %s: %s\n", fbdev->fifo, av_err2str(ret));
        return ret;
    }

    // shmat to attach to shared memory
    fbdev->data = shmat(shmid, (void*)0, 0);
    if (fbdev->data == (void *)(-1)) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR, "shmat failed %s: %s\n", fbdev->fifo, av_err2str(ret));
        return ret;
    }

    // sem
    fbdev->semid = semget(key, 1, 0666|IPC_CREAT);
    if (fbdev->semid == -1) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR, "semget failed %s: %s\n", fbdev->fifo, av_err2str(ret));
        return ret;
    }

    if (sem_init(fbdev->semid) == -1) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR, "sem_init failed %s: %s\n", fbdev->fifo, av_err2str(ret));
        return ret;
    }

    AVStream *st = NULL;
    if (!(st = avformat_new_stream(avctx, NULL))){
        return AVERROR(ENOMEM);
    }
    // avpriv_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in microseconds */
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->width      = fbdev->width;
    st->codecpar->height     = fbdev->height;
    st->codecpar->format     = fbdev->pixelFmt;
    // st->avg_frame_rate       = fbdev->fps;
    // st->codecpar->bit_rate   = fbdev->frame_size * av_q2d(fbdev->fps) * 8;

    fbdev->timeout.tv_sec = fbdev->minfps.den/fbdev->minfps.num;
    fbdev->timeout.tv_nsec = (1e9 * fbdev->minfps.den)/fbdev->minfps.num - 1e9 * (fbdev->timeout.tv_sec);
    return 0;
}

static int fbdev_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    SHMEMDevContext *fbdev = avctx->priv_data;
    int ret;

    struct timespec ts;
    ts.tv_sec  = fbdev->timeout.tv_sec;
    ts.tv_nsec = fbdev->timeout.tv_nsec;
    
    if (P(fbdev->semid, &ts) == -1 && errno != EAGAIN) {
        av_log(avctx, AV_LOG_ERROR, "wait sem failed %s: %s\n", fbdev->fifo, av_err2str(errno));
        return AVERROR(errno);
    }

    if ((ret = av_new_packet(pkt, fbdev->frame_size)) < 0)
        return ret;

    pkt->pts = av_gettime();
    pkt->dts = pkt->pts;

    // int64_t *fts = (int64_t*)((uint8_t*)fbdev->data + fbdev->frame_size);
    // av_log(avctx, AV_LOG_ERROR, "frame %d %" PRId64 "\n", fbdev->frame_size, *fts);

    memcpy(pkt->data, fbdev->data, fbdev->frame_size);
    return fbdev->frame_size;
}

static av_cold int fbdev_read_close(AVFormatContext *avctx)
{
    av_log(avctx, AV_LOG_ERROR, "dev close\n");
    SHMEMDevContext *fbdev = avctx->priv_data;
    return 0;
}

static int fbdev_get_device_list(AVFormatContext *s, AVDeviceInfoList *device_list)
{
    if (!device_list)
        return AVERROR(EINVAL);

    AVDeviceInfo *device = av_mallocz(sizeof(AVDeviceInfo));
    if (!device) {
        return AVERROR(ENOMEM);
    }

    device->device_name = av_strdup("shmem");
    device->device_description = av_strdup("placeholder");
    if (!device->device_name || !device->device_description) {
        return AVERROR(ENOMEM);
    }

    int ret = 0;
    if ((ret = av_dynarray_add_nofree(&device_list->devices,
                                        &device_list->nb_devices, device)) < 0){

        av_freep(&device->device_name);
        av_freep(&device->device_description);
        av_freep(&device);
        return ret;
    }

    return ret;
}

#define OFFSET(x) offsetof(SHMEMDevContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "fifo","", OFFSET(fifo), AV_OPT_TYPE_STRING,
                { .str = "" }, 0, 0, DEC },
    { "minfps","", OFFSET(minfps), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, DEC },
    { "fmt","", OFFSET(pixelFmt), AV_OPT_TYPE_PIXEL_FMT, \
                {.i64 = AV_PIX_FMT_NV12}, -1, INT32_MAX, DEC },
    { "size","", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = "1280x720"}, 0, 0, DEC },
    { NULL },
};

static const AVClass shmemdev_class = {
    .class_name = "shmemdev indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

AVInputFormat ff_shmemdev_demuxer = {
    .name           = "shmemdev",
    .long_name      = NULL_IF_CONFIG_SMALL("Linux sharememory"),
    .priv_data_size = sizeof(SHMEMDevContext),
    .read_header    = fbdev_read_header,
    .read_packet    = fbdev_read_packet,
    .read_close     = fbdev_read_close,
    .get_device_list = fbdev_get_device_list,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &shmemdev_class,
};
