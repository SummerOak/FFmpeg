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

static int P(int semid) {
    struct sembuf sops = {0, -1, 0};
    return semop(semid, &sops, 1);
}

static int V(int semid) {
    struct sembuf sops = {0, +1, 0};
    return semop(semid, &sops, 1);
}


typedef struct SHMEMDevContext {
    AVClass *class;          ///< class for private options
    char *fifo;
    int frame_size;          ///< size in bytes of a grabbed frame
    int64_t time_frame;      ///< time for the next frame to output (in 1/1000000 units)

    int fps;
    int width, height;       ///< assumed frame resolution
    int pixelFmt;

    int semid;
    uint8_t *data;           ///< framebuffer data
} SHMEMDevContext;


static av_cold int fbdev_read_header(AVFormatContext *avctx)
{
    int ret;
    SHMEMDevContext *fbdev = avctx->priv_data;

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
    return 0;
}

static int fbdev_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    SHMEMDevContext *fbdev = avctx->priv_data;
    int64_t curtime, delay;
    struct timespec ts;
    int i, ret;
    uint8_t *pin, *pout;

    if (fbdev->time_frame == AV_NOPTS_VALUE)
        fbdev->time_frame = av_gettime_relative();

    /* wait based on the frame rate */
    while (1) {
        curtime = av_gettime_relative();
        delay = fbdev->time_frame - curtime;
        av_log(avctx, AV_LOG_TRACE,
                "time_frame:%"PRId64" curtime:%"PRId64" delay:%"PRId64"\n",
                fbdev->time_frame, curtime, delay);
        if (delay <= 0) {
            fbdev->time_frame += INT64_C(1000000) / fbdev->fps;
            break;
        }
        if (avctx->flags & AVFMT_FLAG_NONBLOCK)
            return AVERROR(EAGAIN);
        ts.tv_sec  =  delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        while (nanosleep(&ts, &ts) < 0 && errno == EINTR);
    }

    if ((ret = av_new_packet(pkt, fbdev->frame_size)) < 0)
        return ret;

    pkt->pts = av_gettime();

    if (P(fbdev->semid) == -1) {
        av_log(avctx, AV_LOG_ERROR, "wait sem failed %s: %s\n", fbdev->fifo, av_err2str(errno));
        return AVERROR(errno);
    }

    memcpy(pkt->data, fbdev->data, fbdev->frame_size);
    return fbdev->frame_size;
}

static av_cold int fbdev_read_close(AVFormatContext *avctx)
{
    SHMEMDevContext *fbdev = avctx->priv_data;
    return 0;
}

static int fbdev_get_device_list(AVFormatContext *s, AVDeviceInfoList *device_list)
{
    return ff_fbdev_get_device_list(device_list);
}

#define OFFSET(x) offsetof(SHMEMDevContext, x)
#define ENC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "fifo","", OFFSET(fifo), AV_OPT_TYPE_STRING,
                { .str = NULL }, 0, 0, ENC },
    { "fps","", OFFSET(fps), AV_OPT_TYPE_VIDEO_RATE, {.str = "30"}, 0, INT_MAX, ENC },
    { "fmt","", OFFSET(pixelFmt), AV_OPT_TYPE_PIXEL_FMT, \
                {.i64 = AV_PIX_FMT_NONE}, -1, INT32_MAX, ENC },
    { "width","", OFFSET(width), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, ENC },
    { "height","", OFFSET(height), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, ENC },
    { NULL },
};

static const AVClass shmemdev_class = {
    .class_name = "shmem indev",
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
