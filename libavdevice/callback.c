

#include "libavutil/log.h"
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
    CallbackDevContext *dev = h->priv_data;
    dev->cb = (callback*)(dev->pointer);
    dev->cbPriv = (void*)(dev->priv);
    av_log(dev, AV_LOG_DEBUG, "buffer_write_header, dev=%p, callback=%p pointer=%" PRIu64, 
        dev, dev?dev->cb:NULL, dev?dev->pointer:0);

    int codec_type = h->streams[0]->codecpar->codec_type;
    if (h->nb_streams != 1 || codec_type != AVMEDIA_TYPE_VIDEO && codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(dev, AV_LOG_ERROR, 
            "Only a single video stream is supported. nb_streams=%d, codec_type=%d \n", 
            h->nb_streams, codec_type);
        return AVERROR(EINVAL);
    }

    dev->frameIndex = 0;

    return 0;
}

static int buffer_write_packet(AVFormatContext *h, AVPacket *pkt)
{
    CallbackDevContext *dev = h->priv_data;
    av_log(dev, AV_LOG_DEBUG, "buffer_write_packet, dev=%p, callback=%p pointer=%ld priv=%ld", 
        dev, dev?dev->cb:NULL, dev?dev->pointer:0l, dev?dev->priv:0l);
    
    dev->frameIndex++;
    if(dev && dev->cb){
        dev->cb(dev->cbPriv, dev->frameIndex, h, pkt);
    }
    return 0;
}

static av_cold int buffer_write_trailer(AVFormatContext *h)
{
    CallbackDevContext *dev = h->priv_data;
    av_log(dev, AV_LOG_DEBUG, "buffer_write_trailer");
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
    .category   = AV_CLASS_CATEGORY_OUTPUT,
};

AVOutputFormat ff_callback_muxer = {
    .name           = "callback",
    .long_name      = NULL_IF_CONFIG_SMALL("Callback output dev"),
    .priv_data_size = sizeof(CallbackDevContext),
    .audio_codec    = AV_CODEC_ID_PCM_S16LE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = buffer_write_header,
    .write_packet   = buffer_write_packet,
    .write_trailer  = buffer_write_trailer,
    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS | AVFMT_TS_NONSTRICT,
    .priv_class     = &shmdev_class,
};