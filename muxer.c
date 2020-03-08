#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define V 0
#define A 1

typedef struct {
	AVFormatContext *fmt_ctx;
	AVStream *stream;
	int stream_index;
}InputFileInfo;

typedef struct {
	AVFormatContext *fmt_ctx;
	AVStream *stream[2];
	int stream_index[2];
}OutputFileInfo;


static int open_input_file(InputFileInfo *input, int type, const char *filename)
{
	int ret = -1;

	if ((ret = avformat_open_input(&input->fmt_ctx, filename, NULL, NULL)) < 0)
	{
		fprintf(stderr, "avformat_open_input for %s failed.\n", filename);
		goto ret1;
	}

	if ((ret = avformat_find_stream_info(input->fmt_ctx, NULL)) < 0)
	{
		fprintf(stderr, "avformat_find_stream_info for %s failed.\n", filename);
		goto ret2;
	}

	if ((ret = av_find_best_stream(input->fmt_ctx, type, -1, -1, NULL, 0)) < 0)
	{
		fprintf(stderr, "av_find_best_stream for %s failed.\n", filename);
		goto ret2;
	}
	input->stream_index = ret;
	input->stream = input->fmt_ctx->streams[ret];


	return 0;
ret2:
	avformat_close_input(&input->fmt_ctx);
ret1:
	return -1;
}

static int create_new_stream(OutputFileInfo *output, AVStream *input_stream, int num)
{
	int ret = -1;
	AVCodecContext *cdc_ctx = NULL;
	AVStream *output_stream = NULL;

	if ((output_stream = avformat_new_stream(output->fmt_ctx, NULL)) == NULL)
	{
		fprintf(stderr, "avformat_new_stream for %d failed.\n", num);
		goto ret1;
	}

	if ((cdc_ctx = avcodec_alloc_context3(NULL)) == NULL)
	{
		fprintf(stderr, "avcodec_alloc_context3 failed.\n");
		goto ret1;
	}

	if ((ret = avcodec_parameters_to_context(cdc_ctx, input_stream->codecpar)) < 0)
	{
		fprintf(stderr, "avcodec_parameter_to_context for %d failed.\n", num);
		goto ret2;
	}
	cdc_ctx->codec_tag = 0;
	if (output->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		cdc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	if ((ret = avcodec_parameters_from_context(output_stream->codecpar, cdc_ctx)) < 0)
	{
		fprintf(stderr, "avcodec_parameter_from_context for %d failed.\n", num);
		goto ret2;
	}

	output->stream[num] = output_stream;
	output->stream_index[num] = output_stream->index;

	avcodec_free_context(&cdc_ctx);
	return 0;
ret2:
	avcodec_free_context(&cdc_ctx);
ret1:
	return -1;
}

void muxer(const char *input_video, const char *input_audio, const char *output_file)
{
	int ret = -1;
	InputFileInfo input_v = {0}, input_a = {0};
	OutputFileInfo output = {0};
	AVPacket *pkt_v = NULL;
	AVPacket *pkt_a = NULL;
	int flag_v = 0, flag_a = 0;
	int frame_index_v = 0, frame_index_a = 0;

	if ((ret = open_input_file(&input_v, AVMEDIA_TYPE_VIDEO, input_video)) < 0)
	{
		fprintf(stderr, "open_input_file for video failed.\n");
		goto ret1;
	}

	if ((ret = open_input_file(&input_a, AVMEDIA_TYPE_AUDIO, input_audio)) < 0)
	{
		fprintf(stderr, "open_input_file for audio failed.\n");
		goto ret2;
	}

	printf("-------------input--------------------\n");
	av_dump_format(input_v.fmt_ctx, 0, input_video, 0);
	av_dump_format(input_a.fmt_ctx, 0, input_audio, 0);
	printf("--------------------------------------\n");

	if ((ret = avformat_alloc_output_context2(&output.fmt_ctx, NULL, NULL, output_file)) < 0)
	{
		fprintf(stderr, "avformat_alloc_outpot_context2 failed.\n");
		goto ret3;
	}

	if ((ret = create_new_stream(&output, input_v.stream, V)) < 0)
	{
		fprintf(stderr, "create_new_stream for video failed.\n");
		goto ret4;
	}
	if ((ret = create_new_stream(&output, input_a.stream, A)) < 0)
	{
		fprintf(stderr, "create_new_stream for audio failed.\n");
		goto ret4;
	}

	if (!(output.fmt_ctx->oformat->flags & AVFMT_NOFILE))
	{
		if (avio_open(&output.fmt_ctx->pb, output_file, AVIO_FLAG_WRITE) < 0)
		{
			fprintf(stderr, "avio_open failed.\n");
			goto ret4;
		}
	}

	printf("-------------output-------------------\n");
	av_dump_format(output.fmt_ctx, 0, input_video, 1);
	printf("--------------------------------------\n");

	if ((pkt_v = av_packet_alloc()) == NULL)
	{
		fprintf(stderr, "av_packet_alloc_v failed.\n");
		goto ret5;
	}
	if ((pkt_a = av_packet_alloc()) == NULL)
	{
		fprintf(stderr, "av_packet_alloc_a failed.\n");
		goto ret6;
	}

	if ((ret = avformat_write_header(output.fmt_ctx, NULL)) < 0)
	{
		fprintf(stderr, "avformat_write_header failed.\n");
		goto ret7;
	}

	while (1)
	{
		AVPacket *pkt = NULL;
		AVStream *stream_in = NULL;
		AVStream *stream_out = NULL;
		int stream_index = -1;

		if (flag_v == 0)
		{
			if (av_read_frame(input_v.fmt_ctx, pkt_v) < 0)
			{
				printf("read frame for video end.\n");
				break;
			}

			do {
				if (pkt_v->size > 0 && pkt_v->stream_index == input_v.stream_index)
				{
					if (pkt_v->pts == AV_NOPTS_VALUE) 	/*如果此视频帧没有pts，则计算每帧的pts*/
					{
						AVRational time_base = input_v.stream->time_base;
						int64_t per_duration = AV_TIME_BASE / av_q2d(input_v.stream->r_frame_rate);
						pkt_v->pts = (frame_index_v * per_duration) / (av_q2d(time_base) * AV_TIME_BASE);
						pkt_v->dts = pkt_v->pts;
						pkt_v->duration = per_duration / (av_q2d(time_base) * AV_TIME_BASE);	
						frame_index_v++;
					}
				
					break;
				}
			} while (av_read_frame(input_v.fmt_ctx, pkt_v) >= 0);
		}

		if (flag_a == 0)
		{
			if (av_read_frame(input_a.fmt_ctx, pkt_a) < 0)
			{
				printf("read frame for audio end.\n");
				break;
			}
		
			do {
				if (pkt_a->size > 0 && pkt_a->stream_index == input_a.stream_index)
				{
					if (pkt_a->pts == AV_NOPTS_VALUE) 	/*如果此音频帧没有pts，则计算每帧的pts*/
					{
						AVRational time_base = input_a.stream->time_base;
						int64_t per_duration = AV_TIME_BASE * input_a.stream->codecpar->frame_size / input_a.stream->codecpar->sample_rate;
						pkt_a->pts = (frame_index_a * per_duration) / (av_q2d(time_base) * AV_TIME_BASE);
						pkt_a->dts = pkt_a->pts;
						pkt_a->duration = per_duration / (av_q2d(time_base) / AV_TIME_BASE);
						frame_index_a++;
					}

					break;
				}
			} while (av_read_frame(input_a.fmt_ctx, pkt_a) >= 0);
		}

		/*通过av_compare_ts比较，选出应该写入视频帧还是音频帧，<=0:视频帧；>0:音频帧*/
		if (av_compare_ts(pkt_v->pts, input_v.stream->time_base, pkt_a->pts, input_a.stream->time_base) <= 0)
		{
			pkt = pkt_v;
			stream_in = input_v.stream;
			stream_out = output.stream[V];
			stream_index = output.stream_index[V];

			flag_v = 0;
			flag_a = 1;
		}
		else 		/*audio*/
		{
			pkt = pkt_a;
			stream_in = input_a.stream;
			stream_out = output.stream[A];
			stream_index = output.stream_index[A];
		
			flag_v = 1;
			flag_a = 0;
		}
		/*确定写入什么帧后，还需要把pkt->pts转换为以输出文件time_base为单位的值*/
		pkt->pts = av_rescale_q_rnd(pkt->pts, stream_in->time_base, stream_out->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt->dts = av_rescale_q_rnd(pkt->dts, stream_in->time_base, stream_out->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt->duration = av_rescale_q(pkt->duration, stream_in->time_base, stream_out->time_base);
		pkt->pos = -1;
		pkt->stream_index = stream_index;
		
		printf("Write 1 packet.pts=%lld\n", pkt->pts);
		if (av_interleaved_write_frame(output.fmt_ctx, pkt) < 0)
		{
			fprintf(stderr, "av_interleaved_write_frame failed.\n");
			goto ret7;
		}

		av_packet_unref(pkt);
	}

	av_write_trailer(output.fmt_ctx);


ret7:
	av_packet_free(&pkt_a);
ret6:
	av_packet_free(&pkt_v);
ret5:
	if (!(output.fmt_ctx->oformat->flags & AVFMT_NOFILE))
		avio_close(output.fmt_ctx->pb);
ret4:
	avformat_free_context(output.fmt_ctx);
ret3:
	avformat_close_input(&input_a.fmt_ctx);
ret2:
	avformat_close_input(&input_v.fmt_ctx);
ret1:
	return;
}


int main(int argc, const char *argv[])
{
	if (argc < 4)
	{
		fprintf(stderr, "Usage:<input file video> <input file audio> <output file>\n");
		return -1;
	}

	muxer(argv[1], argv[2], argv[3]);
	return 0;
}
