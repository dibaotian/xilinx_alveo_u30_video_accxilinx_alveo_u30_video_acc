#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <xrm.h>
#include <xmaplugin.h>
#include <xma.h>
#include <xvbm.h>

XlnxIPMapping xlnx_ip_mapping[XRM_MAX_NUM_IP] = {
    {"DECODER", "xrmU30DecPlugin", "decoder", "DECODER_MPSOC",
     "kernel_vcu_decoder", 1},
    {"SCALER", "xrmU30ScalPlugin", "scaler", "SCALER_MPSOC", "", 0},
    {"LOOKAHEAD", "xrmU30EncPlugin", "lookahead", "LOOKAHEAD_MPSOC", "", 0},
    {"ENCODER", "xrmU30EncPlugin", "encoder", "ENCODER_MPSOC",
     "kernel_vcu_encoder", 1}};

typedef struct {
	xrmContext*       xrm_ctx;
    int32_t           enc_load;
    int32_t           enc_num;
	XmaEncoderProperties  xma_enc_props;
} XlnxEncoderCtx;

#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))

int32_t Encoder_ReservationQuery(XlnxEncoderCtx *enc_xrm_ctx)
{

    xrmCuPoolProperty enc_cu_pool_prop;
	int32_t func_id = 0;
    char pluginName[XRM_MAX_NAME_LEN];
    xrmPluginFuncParam plg_param;
    memset(&enc_cu_pool_prop, 0, sizeof(enc_cu_pool_prop));

    enc_xrm_ctx->xrm_ctx = xrmCreateContext(XRM_API_VERSION_1);
    if(enc_xrm_ctx->xrm_ctx == NULL) {
        printf("creation of XRM context failed\n");
        return -1;
    }

    if (enc_xrm_ctx->xrm_ctx == NULL){
        return -1;
    }
	enc_xrm_ctx->xma_enc_props.width = 1920*2;
    enc_xrm_ctx->xma_enc_props.height = 1080*2;
    enc_xrm_ctx->xma_enc_props.framerate.numerator   = 60;
    enc_xrm_ctx->xma_enc_props.framerate.denominator = 1;
    memset(&plg_param, 0, sizeof(xrmPluginFuncParam));

    void (*convertXmaPropsToJson)(void* props, char* funcName, char* jsonJob);
    void* handle = dlopen("/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so", RTLD_NOW );
    convertXmaPropsToJson = dlsym(handle, "convertXmaPropsToJson");
    (*convertXmaPropsToJson)(&enc_xrm_ctx->xma_enc_props, "ENCODER", plg_param.input);
    dlclose(handle);
    strcpy(pluginName, "xrmU30EncPlugin");
    if (xrmExecPluginFunc(enc_xrm_ctx->xrm_ctx, pluginName, func_id, &plg_param) != XRM_SUCCESS){
        return -1;
    }
    else {
		printf("==============%s \n",plg_param.output);
        enc_xrm_ctx->enc_load = atoi((char*)(strtok(plg_param.output, " ")));
        enc_xrm_ctx->enc_num = atoi((char*)(strtok(NULL, " ")));
    }

    int32_t cu_num = 0;
    enc_cu_pool_prop.cuListProp.sameDevice = true;
    enc_cu_pool_prop.cuListNum = 1;
    if (enc_xrm_ctx->enc_load > 0){
        strcpy(enc_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "encoder");
        strcpy(enc_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "ENCODER_MPSOC");
        enc_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        enc_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = XRM_PRECISION_1000000_BIT_MASK(enc_xrm_ctx->enc_load);
        cu_num++;
        for(int32_t i = 0; i < enc_xrm_ctx->enc_num; i++){
            strcpy(enc_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "kernel_vcu_encoder");
            strcpy(enc_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "");
            enc_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
            enc_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
            cu_num++;
        }
    }


    enc_cu_pool_prop.cuListProp.cuNum = cu_num;	
    printf("xrmCheckCuPoolAvailableNumV2 execute \n");
    int num_cu_pool = xrmCheckCuPoolAvailableNumV2(enc_xrm_ctx->xrm_ctx, &enc_cu_pool_prop);
    if(num_cu_pool <= 0){
        return -1;
    }
	printf("encoder num_cu_pool is: %d \n", num_cu_pool);

    int enc_res_poolId = xrmCuPoolReserve(enc_xrm_ctx->xrm_ctx, &enc_cu_pool_prop);
    xrmCuPoolResource encCuPoolRes;
    memset(&encCuPoolRes, 0, sizeof(xrmCuPoolResource));
    xrmReservationQuery(enc_xrm_ctx->xrm_ctx, enc_res_poolId, &encCuPoolRes);
    for (int i = 0; i < encCuPoolRes.cuNum; i++) {
        printf("   deviceId is:  %d\n", encCuPoolRes.cuResources[i].deviceId);
        printf("   cuType is:  %d\n", encCuPoolRes.cuResources[i].cuType);//XRM_CU_IPKERNEL = 1;XRM_CU_SOFTKERNEL = 2
    }

    return 0;
}

# if 0
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
# endif 

char filepath[] = "/home/xilinx/Documents/1080p.264";
int main()
{
    // int height;
	// int width;
	// int fps;

	XlnxEncoderCtx *enc_ctx = (XlnxEncoderCtx*)malloc(sizeof(XlnxEncoderCtx));
	memset(enc_ctx, 0, sizeof(XlnxEncoderCtx));
	int ret = Encoder_ReservationQuery(enc_ctx);
	return ret;
}





