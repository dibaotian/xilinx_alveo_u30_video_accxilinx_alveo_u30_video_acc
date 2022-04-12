#ifdef __cplusplus
extern "C" {
#endif

#ifndef _API_H_
#define _API_H_

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <dlfcn.h>

int Encoder_Init();

int Encoder_frame(char *ybuf,char *uvbuf,char *outBuf,int *outlen);

void Encoder_Release();

void Decoder_Init();

int Decoder_frame(unsigned char* inbuffer,unsigned char* outbuffer,int insize);

int dec_write_host_buffer_to_file(unsigned char* hostbuf,FILE* file);

int H264FrameReader_Init(const char* filename);

int H264FrameReader_ReadFrame(unsigned char* outBuf, int* outBufSize);

void H264FrameReader_Free();

#endif 

#ifdef __cplusplus
}
#endif