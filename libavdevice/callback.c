

#include <unistd.h>
#include "libavutil/pixdesc.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavformat/avformat.h"
#include "avdevice.h"

typedef int (*callback)(void* _this, int frameIndex, AVFormatContext *h, AVPacket *pkt);
typedef struct {
    AVClass *class;     //< class for private options
    long pointer;
    long priv;
    void* cbPriv;
    callback cb;
    int frameIndex;
} CallbackDevContext;

static av_cold int buffer_write_header(AVFormatContext *h)
{
    CallbackDevContext *buffer = h->priv_data;
    buffer->cb = (callback*)(buffer->pointer);
    buffer->cbPriv = (void*)(buffer->priv);
    av_log(buffer, AV_LOG_DEBUG, "buffer_write_header, buffer=%p, callback=%p pointer=%" PRIu64, 
        buffer, buffer?buffer->cb:NULL, buffer?buffer->pointer:0);

    if (h->nb_streams != 1 || h->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        av_log(buffer, AV_LOG_ERROR, "Only a single video stream is supported.\n");
        return AVERROR(EINVAL);
    }

    buffer->frameIndex = 0;

    return 0;
}

static int buffer_write_packet(AVFormatContext *h, AVPacket *pkt)
{
    CallbackDevContext *buffer = h->priv_data;
    av_log(buffer, AV_LOG_DEBUG, "buffer_write_packet, buffer=%p, callback=%p pointer=%ld priv=%ld", 
        buffer, buffer?buffer->cb:NULL, buffer?buffer->pointer:0l, buffer?buffer->priv:0l);
    
    if(buffer && buffer->cb){
        buffer->cb(buffer->cbPriv, buffer->frameIndex, h, pkt);
    }
    return 0;
}

static av_cold int buffer_write_trailer(AVFormatContext *h)
{
    CallbackDevContext *buffer = h->priv_data;
    av_log(buffer, AV_LOG_DEBUG, "buffer_write_trailer");
    return 0;
}

#define OFFSET(x) offsetof(CallbackDevContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "cb", "Callback func pointer", OFFSET(pointer), AV_OPT_TYPE_UINT64, {.i64 = 0}, 0, ULONG_MAX, ENC },
    { "priv", "Private data", OFFSET(priv), AV_OPT_TYPE_UINT64, {.i64 = 0}, 0, ULONG_MAX, ENC },
    { NULL }
};

static const AVClass shmdev_class = {
    .class_name = "Callback outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_callback_muxer = {
    .name           = "buffer",
    .long_name      = NULL_IF_CONFIG_SMALL("buffer output dev"),
    .priv_data_size = sizeof(CallbackDevContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = buffer_write_header,
    .write_packet   = buffer_write_packet,
    .write_trailer  = buffer_write_trailer,
    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS | AVFMT_TS_NONSTRICT,
    .priv_class     = &shmdev_class,
};