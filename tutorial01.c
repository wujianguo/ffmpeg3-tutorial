//
//  tutorial01.c
//  ffmpeg3 tutorial
//
//  Created by wujianguo on 2016/12/21.
//  Copyright © 2016 wujianguo. All rights reserved.
//
// gcc -o tutorial01.out tutorial01.c -lavformat -lavcodec -lswscale -I/usr/local/include -L/usr/local/lib
// 

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

static void save_frame(AVFrame *frame, int width, int height, int index) {
    FILE *pFile;
    char szFilename[1024] = {0};
    int  y;

    // Open file
    sprintf(szFilename, "frame%d.ppm", index);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for(y=0; y<height; y++)
        fwrite(frame->data[0]+y*frame->linesize[0], 1, width*3, pFile);

    // Close file
    fclose(pFile);
}

int main(int argc, const char * argv[]) {
    AVFormatContext *ifmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVPacket packet = {0};
    AVFrame *frame = NULL;
    AVFrame *frame_out = NULL;
    uint8_t *buffer = NULL;
    int ret = 0, i = 0;

    if(argc < 2) {
        printf("Please provide a movie file\n");
        return -1;
    }
    av_register_all();
    const char* in_filename = argv[1];
    if((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "avformat_open_input error.\n");
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info error.\n");
        goto end;
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    int video_stream_index = -1;
    for (i=0; i<ifmt_ctx->nb_streams; ++i) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        av_log(NULL, AV_LOG_ERROR, "can not find video stream.\n");
        goto end;
    }

    AVCodec *codec = NULL;
    if ((codec = avcodec_find_decoder(ifmt_ctx->streams[video_stream_index]->codecpar->codec_id)) == NULL) {
        av_log(NULL, AV_LOG_ERROR, "can not find decoder %d.\n", ifmt_ctx->streams[video_stream_index]->codecpar->codec_id);
        goto end;
    }

    codec_ctx = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(codec_ctx, ifmt_ctx->streams[video_stream_index]->codecpar);

    if ((ret = avcodec_open2(codec_ctx, codec, NULL)) != 0) {
        av_log(NULL, AV_LOG_ERROR, "can not open codec.\n");
        goto end;
    }

    frame = av_frame_alloc();
    frame_out = av_frame_alloc();
    int numBytes;
    // Determine required buffer size and allocate buffer
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
    buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
    av_image_fill_arrays(frame_out->data, frame_out->linesize, buffer, AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);

    struct SwsContext *sws_ctx = sws_getContext(codec_ctx->width,
                                                codec_ctx->height,
                                                codec_ctx->pix_fmt,
                                                codec_ctx->width,
                                                codec_ctx->height,
                                                AV_PIX_FMT_RGB24,
                                                SWS_BILINEAR,
                                                NULL,
                                                NULL,
                                                NULL
                                                );

    int frame_index = 0;
    while ((ret = av_read_frame(ifmt_ctx, &packet)) >= 0) {
        if (packet.stream_index != video_stream_index) {
            continue;
        }
        ret = avcodec_send_packet(codec_ctx, &packet);
        if (ret == 0) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == 0) {
                sws_scale(sws_ctx, (uint8_t const * const *)frame->data,
                          frame->linesize, 0, codec_ctx->height,
                          frame_out->data, frame_out->linesize);

                save_frame(frame_out, codec_ctx->width, codec_ctx->height, frame_index);
                av_packet_unref(&packet);
                if (frame_index++ > 5) {
                    break;
                }
            } else if (ret == AVERROR(EAGAIN)) {
                continue;
            } else {
                av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame error.\n");
                break;
            }
        } else if (ret == AVERROR(EAGAIN)) {
            continue;
        } else {
            av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet error.\n");
            break;
        }
    }

end:
    if (frame != NULL) {
        av_frame_free(&frame);
    }
    if (frame_out != NULL) {
        av_frame_free(&frame_out);
    }
    if (buffer != NULL) {
        av_free(buffer);
    }
    if (codec_ctx != NULL) {
        avcodec_close(codec_ctx);
    }
    avformat_close_input(&ifmt_ctx);
    if (ret < 0 && ret != AVERROR_EOF) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
    return ret;
}
