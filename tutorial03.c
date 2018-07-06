//
//  tutorial03.c
//  ffmpeg3 tutorial
//
//  Created by wujianguo on 2018/7/6.
//  Copyright © 2016 wujianguo. All rights reserved.
//
// gcc -o tutorial03.out tutorial03.c -lavformat -lavcodec -lavutil -lswscale -lSDL2 -I/usr/local/include -L/usr/local/lib
//

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <assert.h>
#include <SDL2/SDL.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

typedef struct PacketQueue
{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;

int quit = 0;

void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{

    AVPacketList *pkt1;
    if (av_dup_packet(pkt) < 0)
    {
        return -1;
    }
    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;)
    {

        if (quit)
        {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1)
        {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{

    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1, data_size = 0;

    for (;;)
    {
        while (audio_pkt_size > 0)
        {
            int got_frame = 0;
            len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
            if (len1 < 0)
            {
                /* if error, skip frame */
                audio_pkt_size = 0;
                break;
            }
            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            data_size = 0;
            if (got_frame)
            {
                data_size = av_samples_get_buffer_size(NULL,
                                                       aCodecCtx->channels,
                                                       frame.nb_samples,
                                                       aCodecCtx->sample_fmt,
                                                       1);
                assert(data_size <= buf_size);
                memcpy(audio_buf, frame.data[0], data_size);
            }
            if (data_size <= 0)
            {
                /* No data yet, get more frames */
                continue;
            }
            /* We have data, return it and come back for more later */
            return data_size;
        }
        if (pkt.data)
            av_free_packet(&pkt);

        if (quit)
        {
            return -1;
        }

        if (packet_queue_get(&audioq, &pkt, 1) < 0)
        {
            return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }
}

void audio_callback(void *userdata, Uint8 *stream, int len)
{
    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len1, audio_size;

    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while (len > 0)
    {
        av_log(NULL, AV_LOG_ERROR, "audio_callback");
        if (audio_buf_index >= audio_buf_size)
        {
            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
            if (audio_size < 0)
            {
                /* If error, output silence */
                audio_buf_size = 1024; // arbitrary?
                memset(audio_buf, 0, audio_buf_size);
            }
            else
            {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len)
            len1 = len;
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
    av_log(NULL, AV_LOG_ERROR, "audio_callback end");
}

int main(int argc, const char *argv[])
{
    AVFormatContext *ifmt_ctx = NULL;
    AVCodecContext *p_codec_ctx = NULL, *a_codec_ctx = NULL;
    AVPacket packet = {0};
    AVFrame *frame = NULL;
    AVFrame *frame_out = NULL;
    uint8_t *buffer = NULL;
    int ret = 0, i = 0;

    SDL_Texture *bmp = NULL;
    SDL_Window *screen = NULL;
    SDL_Rect rect;
    SDL_Event event;
    SDL_AudioSpec wanted_spec, spec;

    if (argc < 2)
    {
        printf("Please provide a movie file\n");
        return -1;
    }

    av_register_all();
    // av_log_set_level(AV_LOG_DEBUG);

    const char *in_filename = argv[1];
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avformat_open_input error.\n");
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info error.\n");
        goto end;
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    int video_stream_index = -1;
    int audio_stream_index = -1;
    for (i = 0; i < ifmt_ctx->nb_streams; ++i)
    {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = i;
        }
        else if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = i;
        }
    }

    if (video_stream_index == -1)
    {
        av_log(NULL, AV_LOG_ERROR, "can not find video stream.\n");
        goto end;
    }
    if (audio_stream_index == -1)
    {
        av_log(NULL, AV_LOG_ERROR, "can not find audio stream.\n");
        goto end;
    }

    AVCodec *p_codec = NULL;
    if ((p_codec = avcodec_find_decoder(ifmt_ctx->streams[video_stream_index]->codecpar->codec_id)) == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "can not find decoder %d.\n", ifmt_ctx->streams[video_stream_index]->codecpar->codec_id);
        goto end;
    }
    AVCodec *a_codec = NULL;
    if ((a_codec = avcodec_find_decoder(ifmt_ctx->streams[audio_stream_index]->codecpar->codec_id)) == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "can not find decoder %d.\n", ifmt_ctx->streams[audio_stream_index]->codecpar->codec_id);
        goto end;
    }

    // video
    p_codec_ctx = avcodec_alloc_context3(p_codec);
    avcodec_parameters_to_context(p_codec_ctx, ifmt_ctx->streams[video_stream_index]->codecpar);
    if ((ret = avcodec_open2(p_codec_ctx, p_codec, NULL)) != 0)
    {
        av_log(NULL, AV_LOG_ERROR, "can not open codec.\n");
        goto end;
    }

    // audio
    a_codec_ctx = avcodec_alloc_context3(a_codec);
    avcodec_parameters_to_context(a_codec_ctx, ifmt_ctx->streams[audio_stream_index]->codecpar);
    if ((ret = avcodec_open2(a_codec_ctx, a_codec, NULL)) != 0)
    {
        av_log(NULL, AV_LOG_ERROR, "can not open codec.\n");
        goto end;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }

    wanted_spec.freq = a_codec_ctx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = a_codec_ctx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = a_codec_ctx;

    if (SDL_OpenAudio(&wanted_spec, &spec) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_OpenAudio: %s\n", SDL_GetError());
        goto end;
    }

    packet_queue_init(&audioq);
    SDL_PauseAudio(0);

    screen = SDL_CreateWindow("ffmpeg3 tutorial",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              p_codec_ctx->width, p_codec_ctx->height,
                              0);

    SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, 0);
    bmp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, p_codec_ctx->width, p_codec_ctx->height);

    frame = av_frame_alloc();
    frame_out = av_frame_alloc();
    int numBytes;
    // Determine required buffer size and allocate buffer
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, p_codec_ctx->width, p_codec_ctx->height, 1);
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(frame_out->data, frame_out->linesize, buffer, AV_PIX_FMT_YUV420P, p_codec_ctx->width, p_codec_ctx->height, 1);

    struct SwsContext *sws_ctx = sws_getContext(p_codec_ctx->width,
                                                p_codec_ctx->height,
                                                p_codec_ctx->pix_fmt,
                                                p_codec_ctx->width,
                                                p_codec_ctx->height,
                                                AV_PIX_FMT_YUV420P,
                                                SWS_BILINEAR,
                                                NULL,
                                                NULL,
                                                NULL);

    rect.x = 0;
    rect.y = 0;
    rect.w = p_codec_ctx->width;
    rect.h = p_codec_ctx->height;

    while ((ret = av_read_frame(ifmt_ctx, &packet)) >= 0)
    {
        if (packet.stream_index != video_stream_index)
        {
            continue;
        }
        ret = avcodec_send_packet(p_codec_ctx, &packet);
        if (ret == 0)
        {
            ret = avcodec_receive_frame(p_codec_ctx, frame);
            if (ret == 0)
            {
                sws_scale(sws_ctx, (uint8_t const *const *)frame->data,
                          frame->linesize, 0, p_codec_ctx->height,
                          frame_out->data, frame_out->linesize);

                SDL_UpdateYUVTexture(bmp, &rect,
                                     frame_out->data[0], frame_out->linesize[0],
                                     frame_out->data[1], frame_out->linesize[1],
                                     frame_out->data[2], frame_out->linesize[2]);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, bmp, &rect, &rect);
                SDL_RenderPresent(renderer);
                SDL_Delay(50);
            }
            else if (ret == AVERROR(EAGAIN))
            {
                continue;
            }
            else
            {
                av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame error.\n");
                break;
            }
        }
        else if (ret == AVERROR(EAGAIN))
        {
            continue;
        }
        else
        {
            av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error.\n");
            break;
        }

        SDL_PollEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            quit = 1;
            SDL_Quit();
            goto end;
            break;
        default:
            break;
        }
    }

end:
    if (bmp != NULL)
    {
        SDL_DestroyTexture(bmp);
    }
    if (frame != NULL)
    {
        av_frame_free(&frame);
    }
    if (frame_out != NULL)
    {
        av_frame_free(&frame_out);
    }
    if (buffer != NULL)
    {
        av_free(buffer);
    }
    if (p_codec_ctx != NULL)
    {
        avcodec_close(p_codec_ctx);
    }

    avformat_close_input(&ifmt_ctx);
    if (ret < 0 && ret != AVERROR_EOF)
    {
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
    return ret;
}
