#include "xilinx_encoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>

#if 0 //for encoder
int ReadYUV(char *ybuf, char *uvbuf, FILE *hInputYUVFile)
{
	int ylen = 1920 * 1080;
	int uvlen = 1920 * 1080 / 2; 
	
	fread(ybuf, ylen, 1, hInputYUVFile);
	fread(uvbuf, uvlen, 1, hInputYUVFile);

	return 0;
}

int main()
{
	printf("Stert Encoding \n");
	char* outBuf = (char*)malloc(1920*1080);
	int outlen =0;
	
	static FILE* fin = NULL;
	if (!fin) fin = fopen("./test.yuv", "rb");
	
	static FILE* fin2 = NULL;
	if (!fin2) fin2 = fopen("./xilinx_output.264", "wb");
	
	Encoder_Init();

    for(int i=0;i<900;i++) 
	{
		char ybuf[1920*1080]={0};
		char uvbuf[1920*1080]={0};
		
        ReadYUV(ybuf, uvbuf, fin);
		
		Encoder_frame(ybuf,uvbuf,outBuf,&outlen);
		
		fwrite(outBuf, sizeof(char), outlen, fin2);
		
		printf("==============%d \n",outlen);
    }

    printf("Encoding of input stream completed \n");

    Encoder_Release();

    return 0;
}
#else  //for decoder

char filepath[] = "./1080p.264";
char outputpath[] = "./xilinx_output.yuv";
int main()
{

    int height;
	int width;
	int fps;
	
	unsigned long max_size;
	unsigned char* tmpbuf;

	int tmpbuf_len = 1400;
	int current_read_len = 0;
	
	unsigned char* host_buffer;
	unsigned char* out_buffer;

	int ret = 0;

	ret = get_stream_info(filepath, &height, &width, &fps);
	if(ret == -1){
		return ret;
	}else{

		printf("height = %d \n", height);
		printf("width = %d \n", width);
		printf("fps = %d \n", fps);

		max_size = width*height;
		tmpbuf = (unsigned char*)malloc(max_size * 3);
		host_buffer = (unsigned char*)malloc(1920*1080*3);
		out_buffer = (unsigned char*)malloc(1920*1080*3);
	}
	
	Decoder_Init();

	// Encoder_Init();
	
	int filesize_ = H264FrameReader_Init(filepath);
	printf("file size = %d\n", filesize_);
	while (current_read_len < filesize_)
	{
		if (H264FrameReader_ReadFrame(tmpbuf, &tmpbuf_len))
		{
			//printf("read h264 size = %d\n", tmpbuf_len);
			Decoder_frame(tmpbuf,out_buffer,tmpbuf_len);
			static FILE* fin2 = NULL;
			if (!fin2) fin2 = fopen(outputpath, "wb");
			fwrite(out_buffer, sizeof(char), 960*1080*3, fin2);
			usleep(5000);
			current_read_len += tmpbuf_len;
		}
	}

	//complete release resource
	Decoder_release();

    // free buffer
	if(NULL != tmpbuf){
		free(tmpbuf);
	}

	if(NULL != host_buffer){
		free(host_buffer);
	}

	if(NULL != out_buffer){
		free(out_buffer);
	}

	return 0;
}
#endif

int get_stream_info(const char *filepath, int *height, int *width, int *fps)
{
	AVFormatContext* pFormatCtx;
	static int video_stream_index;	
	// char filepath[] = "./1080p.264";
	// char filepath[] = *video_file;
	// int height = 0;
	// int width = 0;
	// int bitrate = 0;
	int ret = 0;

	// av_register_all();
	//avformat_network_init();
	pFormatCtx = avformat_alloc_context();

	// 1.open video file：get the head，out info in "AVFormatContext pFormatCtx"
	if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0)
	{
		printf("Couldn't open input stream.\n");
		return -1;
	}else{
		av_dump_format(pFormatCtx, 0, filepath, 0);//打印关于输入或输出格式的详细信息，例如持续时间，比特率，流，容器，程序，元数据，边数据，编解码器和时基。
		if (avformat_find_stream_info(pFormatCtx, NULL)<0)
		{
			printf("Couldn't find stream information.\n");
			ret = -1;
		}else{

			video_stream_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
			*height  = pFormatCtx->streams[video_stream_index]->codecpar->height;
			*width = pFormatCtx->streams[video_stream_index]->codecpar->width;
			*fps = pFormatCtx->streams[video_stream_index]->avg_frame_rate.num;
			// bitrate = pFormatCtx->streams[video_stream_index]->codecpar->bit_rate;

			printf("avg_frame_rate num %d \n", pFormatCtx->streams[video_stream_index]->avg_frame_rate.num);
			printf("avg_frame_rate den %d \n", pFormatCtx->streams[video_stream_index]->avg_frame_rate.den);


			// printf("height = %d \n", height);
			// printf("width = %d \n", width);
			// printf("bitrate = %d \n", bitrate);
			// printf("package size = %d \n", pFormatCtx->packet_size);
			// printf("frame size = %d \n ", pFormatCtx->streams[video_stream_index]->codec->frame_size);
		}
	}
	
	avformat_close_input(&pFormatCtx);

	return ret;
}

