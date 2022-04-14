#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <xrm.h>
#include <xmaplugin.h>
#include <xma.h>
#include <xvbm.h>

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
    enc_xrm_ctx->xrm_ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1);
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
    int num_cu_pool = xrmCheckCuPoolAvailableNum(enc_xrm_ctx->xrm_ctx, &enc_cu_pool_prop);
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

int main()
{
	XlnxEncoderCtx *enc_ctx = (XlnxEncoderCtx*)malloc(30000);
	memset(enc_ctx, 0, sizeof(XlnxEncoderCtx));
	int ret = Encoder_ReservationQuery(enc_ctx);
	return ret;
}





