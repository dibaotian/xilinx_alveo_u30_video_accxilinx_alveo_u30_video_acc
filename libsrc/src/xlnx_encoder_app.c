#include "xilinx_encoder.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>
#include <dlfcn.h>
#include <regex.h>
#include <xrm.h>
#include <xmaplugin.h>
#include <xma.h>
#include <xvbm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <termios.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>

#define FLAG_HELP             "help"
#define FLAG_DEVICE_ID        "d"
#define FLAG_STREAM_LOOP      "stream_loop"
#define FLAG_INPUT_FILE       "i"
#define FLAG_CODEC_TYPE       "c:v"
#define FLAG_INPUT_WIDTH      "w"
#define FLAG_INPUT_HEIGHT     "h"
#define FLAG_INPUT_PIX_FMT    "pix_fmt"
#define FLAG_BITRATE          "b:v"
#define FLAG_BIT_RATE         "b"
#define FLAG_FPS              "fps"
#define FLAG_INTRA_PERIOD     "g"
#define FLAG_CONTROL_RATE     "control-rate"
#define FLAG_MAX_BITRATE      "max-bitrate"
#define FLAG_SLICE_QP         "slice-qp"
#define FLAG_MIN_QP           "min-qp"
#define FLAG_MAX_QP           "max-qp"
#define FLAG_NUM_BFRAMES      "bf"
#define FLAG_IDR_PERIOD       "periodicity-idr"
#define FLAG_PROFILE          "profile"
#define FLAG_LEVEL            "level"
#define FLAG_NUM_SLICES       "slices"
#define FLAG_QP_MODE          "qp-mode"
#define FLAG_ASPECT_RATIO     "aspect-ratio"
#define FLAG_SCALING_LIST     "scaling-list"
#define FLAG_LOOKAHEAD_DEPTH  "lookahead-depth"
#define FLAG_TEMPORAL_AQ      "temporal-aq"
#define FLAG_SPATIAL_AQ       "spatial-aq"
#define FLAG_SPATIAL_AQ_GAIN  "spatial-aq-gain"
#define FLAG_QP               "qp"
#define FLAG_NUM_FRAMES       "frames"
#define FLAG_NUM_CORES        "cores"
#define FLAG_TUNE_METRICS     "tune-metrics"
#define FLAG_LATENCY_LOGGING  "latency_logging"
#define FLAG_OUTPUT_FILE      "o"

typedef struct {
    xrmContext*       xrm_ctx;
    xrmCuListResource encode_cu_list_res;
    xrmCuResource     lookahead_cu_res;
    int32_t           device_id;
    int32_t           enc_res_idx;
    int32_t           enc_load;
    int32_t           la_load;
    int32_t           enc_num;
    int32_t           enc_res_in_use;
    int32_t           lookahead_res_inuse;
} XlnxEncoderXrmCtx;

/* HEVC Encoder supported profiles */
typedef enum
{
    ENC_HEVC_MAIN = 0,
    ENC_HEVC_MAIN_INTRA
} XlnxHevcProfiles;

typedef enum
{
    ENCODER_ID_H264 = 0,
    ENCODER_ID_HEVC
} XlnxEncoderCodecID;

typedef enum
{
    LOOKAHEAD_ID_H264 = 0,
    LOOKAHEAD_ID_HEVC
} XlnxLookaheadCodecID;

typedef enum
{
    YUV_NV12_ID = 0,
    YUV_420P_ID

} XlnxInputPixelFormat;

typedef enum
{
    HELP_ARG = 0,
    DEVICE_ID_ARG,
    LOOP_COUNT_ARG,
    INPUT_FILE_ARG,
    ENCODER_ARG,
    INPUT_WIDTH_ARG,
    INPUT_HEIGHT_ARG,
    INPUT_PIX_FMT_ARG,
    BITRATE_ARG,
    FPS_ARG,
    INTRA_PERIOD_ARG,
    CONTROL_RATE_ARG,
    MAX_BITRATE_ARG,
    SLICE_QP_ARG,
    MIN_QP_ARG,
    MAX_QP_ARG,
    NUM_BFRAMES_ARG,
    IDR_PERIOD_ARG,
    PROFILE_ARG,
    LEVEL_ARG,
    NUM_SLICES_ARG,
    QP_MODE_ARG,
    ASPECT_RATIO_ARG,
    SCALING_LIST_ARG,
    LOOKAHEAD_DEPTH_ARG,
    TEMPORAL_AQ_ARG,
    SPATIAL_AQ_ARG,
    SPATIAL_AQ_GAIN_ARG,
    QP_ARG,
    NUM_FRAMES_ARG,
    LATENCY_LOGGING_ARG,
    NUM_CORES_ARG,
    TUNE_METRICS_ARG,
    OUTPUT_FILE_ARG
} XlnxEncArgIdentifiers;

typedef enum
{
    EParamIntraPeriod = 0,
    EParamLADepth,
    EParamEnableHwInBuf,
    EParamSpatialAQMode,
    EParamTemporalAQMode,
    EParamRateControlMode,
    EParamSpatialAQGain,
    EParamNumBFrames,
    EParamCodecType,
    EParamLatencyLogging
} XlnxLAExtParams;

typedef enum {

    ENC_READ_INPUT = 0,
    ENC_LA_PROCESS,
    ENC_LA_FLUSH,
    ENC_SEND_INPUT,
    ENC_GET_OUTPUT,
    ENC_FLUSH,
    ENC_EOF,
    ENC_STOP,
    ENC_DONE
} XlnxEncoderState;

typedef struct
{
    char *key;
    int value;
} XlnxEncProfileLookup;

typedef struct
{
    XmaFormatType      xma_fmt_type;
    XmaFraction        framerate;
    XlnxLookaheadCodecID   codec_type;
    int32_t            width; 
    int32_t            height;
    int32_t            stride;
    int32_t            bits_per_pixel;
    int32_t            gop_size;
    uint32_t           lookahead_depth;
    uint32_t           spatial_aq_mode;
    uint32_t           temporal_aq_mode;
    uint32_t           rate_control_mode;
    uint32_t           spatial_aq_gain;
    uint32_t           num_bframes;
    int32_t            latency_logging;
    uint8_t            enable_hw_buf;
} XlnxLookaheadProperties;

typedef struct
{
    XmaFilterSession   *filter_session;
    XmaFrame           *xma_la_frame;
    XlnxLookaheadProperties la_props;
    int32_t            num_planes;
    uint8_t            bypass;
} XlnxLookaheadCtx;

typedef struct {
    double cpb_size;
    double initial_delay;
    int64_t max_bitrate;
    int64_t bit_rate;
    int32_t width;
    int32_t height;
    int32_t pix_fmt;
    int32_t fps;
    int32_t gop_size;
    int32_t slice_qp;
    int32_t min_qp;  
    int32_t max_qp;
    int32_t codec_id;
    int32_t control_rate;
    int32_t custom_rc;
    int32_t gop_mode;
    int32_t gdr_mode;
    uint32_t num_bframes;
    uint32_t idr_period;
    int32_t profile;
    int32_t level; 
    int32_t tier;
    int32_t num_slices;
    int32_t dependent_slice;
    int32_t slice_size;
    int32_t lookahead_depth;
    int32_t temporal_aq;
    int32_t spatial_aq;
    int32_t spatial_aq_gain;
    int32_t qp_mode;
    int32_t filler_data;
    int32_t aspect_ratio;
    int32_t scaling_list;
    int32_t entropy_mode;
    int32_t loop_filter;
    int32_t constrained_intra_pred;
    int32_t prefetch_buffer;
    int32_t tune_metrics;
    int32_t num_cores;
    int32_t latency_logging;
    uint32_t enable_hw_buf;
    char    *enc_options;
}XlnxEncoderProperties;

/* Encoder Context */
typedef struct {
    XmaDataBuffer         xma_buffer;
    XmaFrame              in_frame;
    XmaEncoderSession     *enc_session;
    XmaFrame              *enc_in_frame;
    XmaFrame              *la_in_frame;
    XlnxEncoderXrmCtx     enc_xrm_ctx;
    XlnxLookaheadCtx      la_ctx;
    XlnxEncoderProperties enc_props;
    size_t                num_frames;
    size_t                in_frame_cnt;
    size_t                out_frame_cnt;
    int32_t               loop_count;
    uint32_t              la_bypass;
    uint32_t              enc_state;
    int32_t               pts;
    FILE                  *in_file;
    FILE                  *out_file;
} XlnxEncoderCtx;

#define ENC_APP_SUCCESS            0
#define ENC_APP_FAILURE            (-1)
#define ENC_APP_DONE               1
#define ENC_APP_STOP               2

#define ENC_DEFAULT_NUM_B_FRAMES   2
#define ENC_DEFAULT_LEVEL          10
#define ENC_DEFAULT_FRAMERATE      25
#define ENC_DEFAULT_SPAT_AQ_GAIN   50
#define ENC_MAX_SPAT_AQ_GAIN       100
#define ENC_DEFAULT_GOP_SIZE       120

#define MAX_ARG_SIZE               64
#define VCU_HEIGHT_ALIGN           64
#define VCU_STRIDE_ALIGN           256

#define ENC_MIN_LOOKAHEAD_DEPTH    0
#define ENC_MAX_LOOKAHEAD_DEPTH    20

#define ENC_SUPPORTED_MIN_WIDTH    64
#define ENC_DEFAULT_WIDTH          1920
#define ENC_SUPPORTED_MAX_WIDTH    3840
#define ENC_MAX_LA_INPUT_WIDTH     1920

#define ENC_SUPPORTED_MIN_HEIGHT   64
#define ENC_DEFAULT_HEIGHT         1080
#define ENC_SUPPORTED_MAX_HEIGHT   2160
#define ENC_MAX_LA_INPUT_HEIGHT    1080

#define XLNX_ENC_LINE_ALIGN(x,LINE_SIZE) (((((size_t)x) + \
                    ((size_t)LINE_SIZE - 1)) & (~((size_t)LINE_SIZE - 1))))

#define ENC_SUPPORTED_MAX_PIXELS   ((ENC_SUPPORTED_MAX_WIDTH) * (ENC_SUPPORTED_MAX_HEIGHT))

#define ENC_MAX_LA_PIXELS          ((ENC_MAX_LA_INPUT_WIDTH) * (ENC_MAX_LA_INPUT_HEIGHT))

#define ENC_DEFAULT_BITRATE        5000
#define ENC_DEFAULT_MAX_BITRATE    (ENC_DEFAULT_BITRATE)
#define ENC_SUPPORTED_MAX_BITRATE  35000000

#define ENC_SUPPORTED_MIN_QP       0
#define ENC_SUPPORTED_MAX_QP       51

#define ENC_OPTION_DISABLE         0
#define ENC_OPTION_ENABLE          1

#define ENC_RC_CONST_QP_MODE       0
#define ENC_RC_CBR_MODE            1
#define ENC_RC_VBR_MODE            2
#define ENC_RC_LOW_LATENCY_MODE    3

#define ENC_DEFAULT_GOP_MODE       0
#define ENC_PYRAMIDAL_GOP_MODE     1
#define ENC_LOW_DELAY_P_MODE       2
#define ENC_LOW_DELAY_B_MODE       3

#define ENC_GDR_DISABLE            0
#define ENC_GDR_VERTICAL_MODE      1
#define ENC_GDR_HORIZONTAL_MODE    2

#define ENC_LEVEL_10               10
#define ENC_LEVEL_11               11
#define ENC_LEVEL_12               12
#define ENC_LEVEL_13               13
#define ENC_LEVEL_20               20
#define ENC_LEVEL_21               21
#define ENC_LEVEL_22               22
#define ENC_LEVEL_30               30
#define ENC_LEVEL_31               31
#define ENC_LEVEL_32               32
#define ENC_LEVEL_40               40
#define ENC_LEVEL_41               41
#define ENC_LEVEL_42               42
#define ENC_LEVEL_50               50
#define ENC_LEVEL_51               51
#define ENC_LEVEL_52               52

#define ENC_UNIFORM_QP_MODE        0
#define ENC_AUTO_QP_MODE           1
#define ENC_RELATIVE_LOAD_QP_MODE  2

#define ENC_ASPECT_RATIO_AUTO      0
#define ENC_ASPECT_RATIO_4_3       1
#define ENC_ASPECT_RATIO_16_9      2
#define ENC_ASPECT_RATIO_NONE      3

#define ENC_CAVLC_MODE             0
#define ENC_CABAC_MODE             1

#define ENC_MAX_OPTIONS_SIZE       2048
#define ENC_MAX_EXT_PARAMS         3

/* H264 Encoder supported profiles */
#define ENC_H264_BASELINE          66
#define ENC_H264_MAIN              77
#define ENC_H264_HIGH              100

#define XLNX_ENC_APP_MODULE        "xlnx_encoder_app"

/* Lookahead constants */
#define XLNX_SCLEVEL1              2
#define XLNX_LA_MAX_NUM_EXT_PARAMS 10
#define XLNX_MAX_LOOKAHEAD_DEPTH   20




#define DEFAULT_DEVICE_ID    -1
#define XMA_PROPS_TO_JSON_SO "/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so"
#define XCLBIN_PARAM_NAME    "/opt/xilinx/xcdr/xclbins/transcode.xclbin"

#define RET_ERROR     XMA_ERROR
#define RET_SUCCESS   XMA_SUCCESS
#define RET_EOF       XMA_SUCCESS + 1
#define RET_EOS       RET_EOF + 1

#define STRIDE_ALIGN  256
#define HEIGHT_ALIGN  64
#define ALIGN(x,align) (((x) + (align) - 1) & ~((align) - 1))

#define UNASSIGNED               INT32_MIN
#define replace_if_unset(a,b)    ((a == UNASSIGNED) ? (b) : (a))

#define H265_CODEC_TYPE 1
#define HEVC_CODEC_TYPE H265_CODEC_TYPE
#define H265_CODEC_NAME "mpsoc_vcu_hevc"
#define HEVC_CODEC_NAME H265_CODEC_NAME

#define H264_CODEC_TYPE 0
#define AVC_CODEC_TYPE  H264_CODEC_TYPE
#define H264_CODEC_NAME "mpsoc_vcu_h264"
#define AVC_CODEC_NAME  H264_CODEC_NAME

#define DEBUG_LOGLEVEL           0
#define XLNX_APP_UTILS_MODULE    "xlnx_app_utils"
#define XLNX_APP_UTILS_LOG_ERROR(msg...) \
            xma_logmsg(XMA_ERROR_LOG, XLNX_APP_UTILS_MODULE, msg)
#define XLNX_APP_UTILS_LOG_INFO(msg...) \
            xma_logmsg(XMA_INFO_LOG, XLNX_APP_UTILS_MODULE, msg)

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))

static const char *XLNX_LA_EXT_PARAMS[] = {
    "ip",
    "lookahead_depth",
    "enable_hw_in_buf",
    "spatial_aq_mode",
    "temporal_aq_mode",
    "rate_control_mode",
    "spatial_aq_gain",
    "num_b_frames",
    "codec_type",
    "latency_logging"
};

#define DEFAULT_DEVICE_ID    -1
#define XMA_PROPS_TO_JSON_SO "/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so"
#define XCLBIN_PARAM_NAME    "/opt/xilinx/xcdr/xclbins/transcode.xclbin"

#define RET_ERROR     XMA_ERROR
#define RET_SUCCESS   XMA_SUCCESS
#define RET_EOF       XMA_SUCCESS + 1
#define RET_EOS       RET_EOF + 1

#define STRIDE_ALIGN  256
#define HEIGHT_ALIGN  64
#define ALIGN(x,align) (((x) + (align) - 1) & ~((align) - 1))

#define MAX_DEC_PARAMS             11
#define MAX_DEC_WIDTH              3840
#define MAX_DEC_HEIGHT             2160

#define DEFAULT_ENTROPY_BUFF_COUNT 2
#define MIN_ENTROPY_BUFF_COUNT     2
#define MAX_ENTROPY_BUFF_COUNT     10

#define DEC_APP_ERROR              XMA_ERROR
#define DEC_APP_SUCCESS            XMA_SUCCESS

#define XLNX_DEC_APP_MODULE     "xlnx_decoder"
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#define DECODER_APP_LOG_ERROR(msg...) \
            xma_logmsg(XMA_ERROR_LOG, XLNX_DEC_APP_MODULE, msg)
#define DECODER_APP_LOG_INFO(msg...) \
            xma_logmsg(XMA_INFO_LOG, XLNX_DEC_APP_MODULE, msg)

typedef struct XlnxDecoderProperties
{
    int32_t  device_id; // -1 by default
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t log_level;
    uint32_t bit_depth;
    uint32_t codec_type;
    uint32_t low_latency;
    uint32_t entropy_buf_cnt;
    uint32_t zero_copy;
    uint32_t profile_idc;
    uint32_t level_idc;
    uint32_t chroma_mode;
    uint32_t scan_type;
    uint32_t latency_logging;
    uint32_t splitbuff_mode;
} XlnxDecoderProperties;

typedef struct XlnxDecoderXrmCtx
{
    int                       xrm_reserve_id;
    int                       dec_load;
    int                       decode_res_in_use;
    xrmContext*               xrm_ctx;
    xrmCuListResource         decode_cu_list_res;
} XlnxDecoderXrmCtx;

typedef struct XlnxDecoderChannelCtx
{
    FILE*           out_fp;
    size_t          num_frames_to_decode;
    XmaFrame*       xframe;
} XlnxDecoderChannelCtx;

typedef struct XlnxDecoderCtx
{
    int32_t                   pts;
    bool                      is_flush_sent;
    size_t                    num_frames_sent;
    size_t                    num_frames_decoded;
    XmaDecoderSession*        xma_dec_session;
    XmaDecoderProperties      dec_xma_props;
    XlnxDecoderProperties     dec_params;
	XlnxDecoderChannelCtx     channel_ctx;
    XlnxDecoderXrmCtx         dec_xrm_ctx;
} XlnxDecoderCtx;

void xlnx_dec_cleanup_decoder_props(XmaDecoderProperties* dec_xma_props)
{
    if(dec_xma_props->params) {
        free(dec_xma_props->params);
    }
}

static int32_t fill_custom_xma_params(XlnxDecoderProperties* param_ctx, 
                                          XmaDecoderProperties* dec_xma_props)
{
    dec_xma_props->params[0].name   = "bitdepth";
    dec_xma_props->params[0].type   = XMA_UINT32;
    dec_xma_props->params[0].length = sizeof(param_ctx->bit_depth);
    dec_xma_props->params[0].value  = &(param_ctx->bit_depth);

    dec_xma_props->params[1].name   = "codec_type";
    dec_xma_props->params[1].type   = XMA_UINT32;
    dec_xma_props->params[1].length = sizeof(param_ctx->codec_type);
    dec_xma_props->params[1].value  = &(param_ctx->codec_type);

    dec_xma_props->params[2].name   = "low_latency";
    dec_xma_props->params[2].type   = XMA_UINT32;
    dec_xma_props->params[2].length = sizeof(param_ctx->low_latency);
    dec_xma_props->params[2].value  = &(param_ctx->low_latency);

    dec_xma_props->params[3].name   = "entropy_buffers_count";
    dec_xma_props->params[3].type   = XMA_UINT32;
    dec_xma_props->params[3].length = sizeof(param_ctx->entropy_buf_cnt);
    dec_xma_props->params[3].value  = &(param_ctx->entropy_buf_cnt);

    dec_xma_props->params[4].name   = "zero_copy";
    dec_xma_props->params[4].type   = XMA_UINT32;
    dec_xma_props->params[4].length = sizeof(param_ctx->zero_copy);
    dec_xma_props->params[4].value  = &(param_ctx->zero_copy);

    dec_xma_props->params[5].name   = "profile";
    dec_xma_props->params[5].type   = XMA_UINT32;
    dec_xma_props->params[5].length = sizeof(param_ctx->profile_idc);
    dec_xma_props->params[5].value  = &(param_ctx->profile_idc);

    dec_xma_props->params[6].name   = "level";
    dec_xma_props->params[6].type   = XMA_UINT32;
    dec_xma_props->params[6].length = sizeof(param_ctx->level_idc);
    dec_xma_props->params[6].value  = &(param_ctx->level_idc);

    dec_xma_props->params[7].name   = "chroma_mode";
    dec_xma_props->params[7].type   = XMA_UINT32;
    dec_xma_props->params[7].length = sizeof(param_ctx->chroma_mode);
    dec_xma_props->params[7].value  = &(param_ctx->chroma_mode);

    dec_xma_props->params[8].name   = "scan_type";
    dec_xma_props->params[8].type   = XMA_UINT32;
    dec_xma_props->params[8].length = sizeof(param_ctx->scan_type);
    dec_xma_props->params[8].value  = &(param_ctx->scan_type);

    dec_xma_props->params[9].name   = "latency_logging";
    dec_xma_props->params[9].type   = XMA_UINT32;
    dec_xma_props->params[9].length = sizeof(param_ctx->latency_logging);
    dec_xma_props->params[9].value  = &(param_ctx->latency_logging);

    dec_xma_props->params[10].name   = "splitbuff_mode";
    dec_xma_props->params[10].type   = XMA_UINT32;
    dec_xma_props->params[10].length = sizeof(param_ctx->splitbuff_mode);
    dec_xma_props->params[10].value  = &(param_ctx->splitbuff_mode);

    return DEC_APP_SUCCESS;
}

int32_t xlnx_dec_create_xma_props(XlnxDecoderProperties* param_ctx, 
                                      XmaDecoderProperties* dec_xma_props)
{
    strcpy (dec_xma_props->hwvendor_string, "MPSoC");
    dec_xma_props->hwdecoder_type         = XMA_MULTI_DECODER_TYPE;
    dec_xma_props->dev_index              = param_ctx->device_id;
    dec_xma_props->width                  = param_ctx->width;
    dec_xma_props->height                 = param_ctx->height;
    dec_xma_props->bits_per_pixel         = param_ctx->bit_depth;
    dec_xma_props->framerate.numerator    = param_ctx->fps;
    dec_xma_props->framerate.denominator  = 1;
    dec_xma_props->param_cnt              = MAX_DEC_PARAMS;
    dec_xma_props->params                 = calloc(1, MAX_DEC_PARAMS * 
                                            sizeof(XmaParameter));
    fill_custom_xma_params(param_ctx, dec_xma_props);

    return DEC_APP_SUCCESS;
}

int32_t xlnx_dec_create_context(XlnxDecoderCtx* ctx)
{
	XlnxDecoderXrmCtx dec_xrm_ctx;
    XlnxDecoderProperties* param_ctx  = &ctx->dec_params;
	
    param_ctx->device_id          =  DEFAULT_DEVICE_ID;//arguments.device_id;
    param_ctx->fps                =  60;//parse_data.fr_num / parse_data.fr_den;
    param_ctx->width              =  1920;//parse_data.width;
    param_ctx->height             =  1080;//parse_data.height;
    param_ctx->entropy_buf_cnt    =  DEFAULT_ENTROPY_BUFF_COUNT;//arguments.entropy_buf_cnt;
    param_ctx->zero_copy          =  1; 
    param_ctx->codec_type         =  0;//arguments.decoder; // H264 0, H265 1
    param_ctx->bit_depth          =  8; 
    param_ctx->chroma_mode        =  420;
    /* 0 = unknown; 1 = progressive; 2 = top first; 3 = bottom first */
    param_ctx->scan_type          = 1;
    param_ctx->profile_idc = 100;
    param_ctx->level_idc = 40;
	
	XmaFrame*             xframe        = calloc(1, sizeof(*xframe));
    xframe->side_data                   = NULL;
    xframe->frame_props.format          = XMA_VCU_NV12_FMT_TYPE;
    xframe->frame_props.width           = param_ctx->width;
    xframe->frame_props.height          = param_ctx->height;
    xframe->frame_props.bits_per_pixel  = param_ctx->bit_depth;
    xframe->frame_rate.numerator        = param_ctx->fps;
    xframe->frame_rate.denominator      = 1;
    for(int i = 0; i < xma_frame_planes_get(&xframe->frame_props); i++) {
        xframe->data[i].buffer       = NULL;
        xframe->data[i].buffer_type  = XMA_DEVICE_BUFFER_TYPE;
        xframe->data[i].refcount     = 1;
        xframe->data[i].is_clone     = 1;
    }
	
    xlnx_dec_create_xma_props(param_ctx, &ctx->dec_xma_props);

    ctx->channel_ctx.xframe = xframe;
    memset(&dec_xrm_ctx, 0, sizeof(dec_xrm_ctx));
    dec_xrm_ctx.xrm_reserve_id    = 0;
    dec_xrm_ctx.dec_load          = 0;
    dec_xrm_ctx.decode_res_in_use = 0;
    memset(&dec_xrm_ctx.decode_cu_list_res, 0, sizeof(dec_xrm_ctx.decode_cu_list_res));
	ctx->dec_xrm_ctx = dec_xrm_ctx;
	
    return DEC_APP_SUCCESS;
}

void xlnx_dec_cleanup_xrm_ctx(XlnxDecoderXrmCtx* dec_xrm_ctx)
{
    if(!dec_xrm_ctx->xrm_ctx) {
        return;
    }
    if(dec_xrm_ctx->decode_res_in_use) {
        /* Release the resource (still reserved) */
        xrmCuListRelease(dec_xrm_ctx->xrm_ctx, 
                        &dec_xrm_ctx->decode_cu_list_res);
        
        dec_xrm_ctx->decode_res_in_use = 0;
    }
    if(dec_xrm_ctx->xrm_reserve_id) {
        /* Put the resource back into the pool of available. */
        xrmCuPoolRelinquish(dec_xrm_ctx->xrm_ctx, 
                            dec_xrm_ctx->xrm_reserve_id); 
    }
    if(dec_xrm_ctx->xrm_ctx) {
        xrmDestroyContext(dec_xrm_ctx->xrm_ctx);
    }
}

int32_t xlnx_dec_cu_alloc_device_id(XlnxDecoderXrmCtx* dec_xrm_ctx, 
                                    XmaDecoderProperties* dec_props)
{
    dec_xrm_ctx->decode_res_in_use = 0;
    
    /* XRM decoder allocation */
    xrmCuListProperty decode_cu_list_prop;

    memset(&decode_cu_list_prop, 0, sizeof(xrmCuListProperty));
    memset(&dec_xrm_ctx->decode_cu_list_res, 0, sizeof(xrmCuListResource));

    decode_cu_list_prop.cuNum = 2;
    strcpy(decode_cu_list_prop.cuProps[0].kernelName, "decoder");
    strcpy(decode_cu_list_prop.cuProps[0].kernelAlias, "DECODER_MPSOC");
    decode_cu_list_prop.cuProps[0].devExcl = false;
    decode_cu_list_prop.cuProps[0].requestLoad = XRM_PRECISION_1000000_BIT_MASK(
                                        dec_xrm_ctx->dec_load);
    decode_cu_list_prop.cuProps[0].poolId = dec_xrm_ctx->xrm_reserve_id;

    strcpy(decode_cu_list_prop.cuProps[1].kernelName, "kernel_vcu_decoder");
    decode_cu_list_prop.cuProps[1].devExcl = false;
    decode_cu_list_prop.cuProps[1].requestLoad = XRM_PRECISION_1000000_BIT_MASK(
                                        XRM_MAX_CU_LOAD_GRANULARITY_1000000);
    decode_cu_list_prop.cuProps[1].poolId = dec_xrm_ctx->xrm_reserve_id;

    int32_t ret;
    int32_t device_id = dec_props->dev_index;
    ret = xrmCuAllocFromDev(dec_xrm_ctx->xrm_ctx, device_id, 
                            &decode_cu_list_prop.cuProps[0], 
                            &dec_xrm_ctx->decode_cu_list_res.cuResources[0]);
    if(ret != XMA_SUCCESS) {
        DECODER_APP_LOG_ERROR("xrm failed to allocate decoder resources on "
                              "device %d\n", dec_xrm_ctx->xrm_reserve_id);
        return ret;
    }
    ret = xrmCuAllocFromDev(dec_xrm_ctx->xrm_ctx, device_id, 
                            &decode_cu_list_prop.cuProps[1], 
                            &dec_xrm_ctx->decode_cu_list_res.cuResources[1]);
    if(ret != XMA_SUCCESS) {
        DECODER_APP_LOG_ERROR("xrm failed to allocate decoder resources on "
                              "device %d\n", dec_xrm_ctx->xrm_reserve_id);
        return ret;
    }
    dec_xrm_ctx->decode_res_in_use = 1;

    /*Set XMA plugin SO and device index */
    dec_props->plugin_lib     = dec_xrm_ctx->decode_cu_list_res.cuResources[0].
                                kernelPluginFileName;
    /* XMA to select the ddr bank based on xclbin meta data */
    dec_props->ddr_bank_index = -1; 
    dec_props->cu_index       = dec_xrm_ctx->decode_cu_list_res.cuResources[1].
                                cuId;
    dec_props->channel_id     = dec_xrm_ctx->decode_cu_list_res.cuResources[1].
                                channelId; // SW kernel always used 100%
    DECODER_APP_LOG_INFO("Device ID: %d\n", dec_props->dev_index);
    return DEC_APP_SUCCESS;
}

int32_t xlnx_dec_allocate_xrm_dec_cu(XlnxDecoderXrmCtx* dec_xrm_ctx, 
                                     XmaDecoderProperties* dec_props)
{
    dec_xrm_ctx->decode_res_in_use = 0;
    /* XRM decoder allocation */
    xrmCuListProperty decode_cu_list_prop;
    memset(&decode_cu_list_prop, 0, sizeof(xrmCuListProperty));
    memset(&dec_xrm_ctx->decode_cu_list_res, 0, sizeof(xrmCuListResource));

    decode_cu_list_prop.cuNum = 2;
    strcpy(decode_cu_list_prop.cuProps[0].kernelName, "decoder");
    strcpy(decode_cu_list_prop.cuProps[0].kernelAlias, "DECODER_MPSOC");
    decode_cu_list_prop.cuProps[0].devExcl = false;
    decode_cu_list_prop.cuProps[0].requestLoad = XRM_PRECISION_1000000_BIT_MASK(
                                        dec_xrm_ctx->dec_load);
    decode_cu_list_prop.cuProps[0].poolId = dec_xrm_ctx->xrm_reserve_id;

    strcpy(decode_cu_list_prop.cuProps[1].kernelName, "kernel_vcu_decoder");
    decode_cu_list_prop.cuProps[1].devExcl = false;
    decode_cu_list_prop.cuProps[1].requestLoad = XRM_PRECISION_1000000_BIT_MASK(
                                        XRM_MAX_CU_LOAD_GRANULARITY_1000000);
    decode_cu_list_prop.cuProps[1].poolId = dec_xrm_ctx->xrm_reserve_id;

    if(xrmCuListAlloc(dec_xrm_ctx->xrm_ctx, &decode_cu_list_prop, 
                      &dec_xrm_ctx->decode_cu_list_res) != 0) {
        DECODER_APP_LOG_ERROR("xrm_allocation: fail to allocate cu list "
                              "from reserve id %d\n", 
                              dec_xrm_ctx->xrm_reserve_id);
        return DEC_APP_ERROR;
    }
    dec_xrm_ctx->decode_res_in_use = 1;

    /* Set XMA plugin SO and device index */
    dec_props->plugin_lib     = dec_xrm_ctx->decode_cu_list_res.cuResources[0].
                                kernelPluginFileName;
    dec_props->dev_index      = dec_xrm_ctx->decode_cu_list_res.cuResources[0].
                                deviceId;
    /* XMA to select the ddr bank based on xclbin meta data */
    dec_props->ddr_bank_index = -1; 
    dec_props->cu_index       = dec_xrm_ctx->decode_cu_list_res.cuResources[1].
                                cuId;
    dec_props->channel_id     = dec_xrm_ctx->decode_cu_list_res.cuResources[1].
                                channelId; // SW kernel always used 100%
    DECODER_APP_LOG_INFO("Device ID: %d\n", 
               dec_props->dev_index);
    return DEC_APP_SUCCESS;
}

static int32_t dec_fill_pool_props(xrmCuPoolProperty* dec_cu_pool_prop, 
                                   int dec_load)
{
    int32_t cu_num = 0;
    dec_cu_pool_prop->cuListProp.sameDevice = true;
    dec_cu_pool_prop->cuListNum = 1;
    strcpy(dec_cu_pool_prop->cuListProp.cuProps[cu_num].kernelName, 
           "decoder");
    strcpy(dec_cu_pool_prop->cuListProp.cuProps[cu_num].kernelAlias, 
           "DECODER_MPSOC");
    dec_cu_pool_prop->cuListProp.cuProps[cu_num].devExcl = false;
    dec_cu_pool_prop->cuListProp.cuProps[cu_num].requestLoad = 
                                    XRM_PRECISION_1000000_BIT_MASK(dec_load);
    cu_num++;
    strcpy(dec_cu_pool_prop->cuListProp.cuProps[cu_num].kernelName, 
           "kernel_vcu_decoder");
    strcpy(dec_cu_pool_prop->cuListProp.cuProps[cu_num].kernelAlias, "");
    dec_cu_pool_prop->cuListProp.cuProps[cu_num].devExcl = false;
    dec_cu_pool_prop->cuListProp.cuProps[cu_num].requestLoad = 
                                    XRM_PRECISION_1000000_BIT_MASK(
                                        XRM_MAX_CU_LOAD_GRANULARITY_1000000);
    cu_num++;
    dec_cu_pool_prop->cuListProp.cuNum = cu_num;
    return DEC_APP_SUCCESS;
}

static int32_t dec_load_calc(XlnxDecoderXrmCtx* dec_xrm_ctx, 
                             XmaDecoderProperties* dec_props, int* dec_load)
{
    int32_t func_id = 0;
    int32_t ret;
    char pluginName[XRM_MAX_NAME_LEN];
    xrmPluginFuncParam plg_param;

    memset(&plg_param, 0, sizeof(xrmPluginFuncParam));
    void *handle;
    void (*convertXmaPropsToJson)(void* props, char* funcName, char* jsonJob);

    /* Loading propstojson library to convert decoder properties to json */
    handle = dlopen(XMA_PROPS_TO_JSON_SO, RTLD_NOW );

    convertXmaPropsToJson = dlsym(handle, "convertXmaPropsToJson");
    (*convertXmaPropsToJson) (dec_props, "DECODER", plg_param.input);
    dlclose(handle);

    strcpy(pluginName, "xrmU30DecPlugin");

    if((ret = xrmExecPluginFunc(dec_xrm_ctx->xrm_ctx, pluginName, func_id, 
                                &plg_param)) != XRM_SUCCESS) {
        DECODER_APP_LOG_ERROR("XRM decoder plugin failed to calculate decoder "
                              "load. %d\n", ret);
        return DEC_APP_ERROR;
    }
    *dec_load = atoi((char*)(strtok(plg_param.output, " ")));
    return DEC_APP_SUCCESS;
}

static int32_t dec_reserve_xrm_device_id(XlnxDecoderXrmCtx* dec_xrm_ctx, 
                                         XmaDecoderProperties* dec_props,
                                         xrmCuPoolProperty* dec_cu_pool_prop)
{
    int device_id = dec_props->dev_index;
    if(device_id != -1) {
        /* xclbin configuration */
        XmaXclbinParameter xclbin_param;
        xclbin_param.device_id = device_id;
        xclbin_param.xclbin_name = XCLBIN_PARAM_NAME; 
        if(xma_initialize(&xclbin_param, 1) != XMA_SUCCESS) {
            DECODER_APP_LOG_ERROR("XMA Initialization failed\n");
            xlnx_dec_cleanup_xrm_ctx(dec_xrm_ctx);
            return DEC_APP_ERROR;
        }
        DECODER_APP_LOG_INFO("XMA initialization success \n");
        return DEC_APP_SUCCESS;
    }
    /* Query XRM to get reservation index for the required CU */
    dec_xrm_ctx->xrm_reserve_id = xrmCuPoolReserve(dec_xrm_ctx->xrm_ctx, 
                                                   dec_cu_pool_prop);
    if(dec_xrm_ctx->xrm_reserve_id == 0) {
        DECODER_APP_LOG_ERROR("xrm_cu_pool_reserve: fail to reserve decode "
                              "cu pool\n");
        xlnx_dec_cleanup_xrm_ctx(dec_xrm_ctx);
        return DEC_APP_ERROR;
    }
    xrmCuPoolResource cu_pool_res;
    memset(&cu_pool_res, 0, sizeof(cu_pool_res));
    DECODER_APP_LOG_INFO("Reservation index %d \n", 
                         dec_xrm_ctx->xrm_reserve_id);
    /* Query XRM for the CU pool resource details like xclbinname and device 
     * ID */
    
    if(xrmReservationQuery(dec_xrm_ctx->xrm_ctx, dec_xrm_ctx->xrm_reserve_id, 
       &cu_pool_res) != 0) {
        DECODER_APP_LOG_ERROR("xrm_reservation_query: fail to query reserved "
                              "cu list\n");
        xlnx_dec_cleanup_xrm_ctx(dec_xrm_ctx);
        return DEC_APP_ERROR;
    }
    /* xclbin configuration */
    XmaXclbinParameter xclbin_param;
    xclbin_param.device_id = cu_pool_res.cuResources[0].deviceId;
    xclbin_param.xclbin_name = cu_pool_res.cuResources[0].xclbinFileName; 

    if((xma_initialize(&xclbin_param, 1)) != XMA_SUCCESS) {
        DECODER_APP_LOG_ERROR("XMA Initialization failed\n");
        xlnx_dec_cleanup_xrm_ctx(dec_xrm_ctx);
        return DEC_APP_ERROR;
    }
    DECODER_APP_LOG_INFO("XMA initialization success \n");
    return DEC_APP_SUCCESS;
}

int32_t xlnx_dec_reserve_xrm_resource(XlnxDecoderXrmCtx* dec_xrm_ctx, 
                                      XmaDecoderProperties* dec_props)
{
    xrmCuPoolProperty dec_cu_pool_prop;
    int32_t ret = DEC_APP_ERROR;
    int32_t num_cu_pool = 0;

    memset(&dec_cu_pool_prop, 0, sizeof(dec_cu_pool_prop));

    /* Create XRM context */
    dec_xrm_ctx->xrm_ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1);
    if(dec_xrm_ctx->xrm_ctx == NULL) {
        DECODER_APP_LOG_ERROR("Create local XRM context failed\n");
        return DEC_APP_ERROR;
    }

    /* Calculate decoder load based on decoder properties */
    int dec_load;
    ret = dec_load_calc(dec_xrm_ctx, dec_props, &dec_load);
    if(ret != DEC_APP_SUCCESS) {
        DECODER_APP_LOG_ERROR("Decoder load calculation failed %d \n", ret);
        return ret;
    }
    dec_fill_pool_props(&dec_cu_pool_prop, dec_load);
    dec_xrm_ctx->dec_load = dec_load;

    /* Check the number of pools available for the given encoder load */
    num_cu_pool = xrmCheckCuPoolAvailableNum(dec_xrm_ctx->xrm_ctx, 
                                             &dec_cu_pool_prop);
    if(num_cu_pool <= 0) {
        DECODER_APP_LOG_ERROR("No resources available for allocation \n");
        return DEC_APP_ERROR;
    }
    DECODER_APP_LOG_INFO("Num CU pools available %d \n", num_cu_pool);

    /* If the device reservation ID is not sent through command line get the 
     * next available device id */
    dec_reserve_xrm_device_id(dec_xrm_ctx, dec_props, &dec_cu_pool_prop);
    if(ret != DEC_APP_SUCCESS) {
        DECODER_APP_LOG_ERROR("xrm_allocation_query: fail to query allocated "
                              "cu list\n");
        return DEC_APP_ERROR;
    }
    return DEC_APP_SUCCESS;
}

int32_t xlnx_dec_fpga_init(XlnxDecoderCtx* ctx)
{
    // Reserve xrm resource and xma initialize
    int32_t ret = xlnx_dec_reserve_xrm_resource(&ctx->dec_xrm_ctx, 
                                           &ctx->dec_xma_props);
    if(ret != DEC_APP_SUCCESS) {
        return DEC_APP_ERROR;
    }
    if(ctx->dec_params.device_id == -1) {
        return xlnx_dec_allocate_xrm_dec_cu(&ctx->dec_xrm_ctx, 
                                       &ctx->dec_xma_props);
    }
    return xlnx_dec_cu_alloc_device_id(&ctx->dec_xrm_ctx, 
                                  &ctx->dec_xma_props);
}

void xlnx_dec_cleanup_ctx(XlnxDecoderCtx* ctx)
{
    if(!ctx) {
        return;
    }
    if(ctx->xma_dec_session) {
        xma_dec_session_destroy(ctx->xma_dec_session);
    }
    xlnx_dec_cleanup_xrm_ctx(&ctx->dec_xrm_ctx);
    xlnx_dec_cleanup_decoder_props(&ctx->dec_xma_props);
}

uint8_t* xlnx_dec_get_buffer_from_fpga(XlnxDecoderCtx* ctx, size_t*buffer_size)
{
    int aligned_width, aligned_height;
    int ret = XMA_ERROR;
    XmaFrame* decoded_frame = ctx->channel_ctx.xframe;
    uint8_t* host_buffer;
    aligned_width  = ALIGN(ctx->dec_params.width, STRIDE_ALIGN);
    aligned_height = ALIGN(ctx->dec_params.height, HEIGHT_ALIGN);
    *buffer_size  = aligned_width * aligned_height + // Y plane
                    (aligned_width * aligned_height) / 2; // UV plane

    host_buffer = (uint8_t *)xvbm_buffer_get_host_ptr(decoded_frame->data[0].
                                                      buffer);
    ret = xvbm_buffer_read(decoded_frame->data[0].buffer, host_buffer, 
                           (*buffer_size), 0);
    if(ret != XMA_SUCCESS) {
        DECODER_APP_LOG_ERROR("xvbm_buffer_read failed\n");
        xlnx_dec_cleanup_ctx(ctx);
        exit(DEC_APP_ERROR);
    }
    xvbm_buffer_pool_entry_free(decoded_frame->data[0].buffer);
    return host_buffer;
}

static int32_t xlnx_enc_xma_params_update(XlnxEncoderProperties *enc_props, 
                                          XmaEncoderProperties *xma_enc_props)
{
    int32_t param_cnt = 0;

    xma_enc_props->params[param_cnt].name   = "enc_options";
    xma_enc_props->params[param_cnt].type   = XMA_STRING;
    xma_enc_props->params[param_cnt].length = strlen(enc_props->enc_options);
    xma_enc_props->params[param_cnt].value  = &(enc_props->enc_options);
    param_cnt++;

    xma_enc_props->params[param_cnt].name   = "latency_logging";
    xma_enc_props->params[param_cnt].type   = XMA_UINT32;
    xma_enc_props->params[param_cnt].length = 
                                      sizeof(enc_props->latency_logging);
    xma_enc_props->params[param_cnt].value  = &(enc_props->latency_logging);
    param_cnt++;

    xma_enc_props->params[param_cnt].name = "enable_hw_in_buf";
    xma_enc_props->params[param_cnt].type = XMA_UINT32;
    xma_enc_props->params[param_cnt].length = 
                                      sizeof(enc_props->enable_hw_buf);
    xma_enc_props->params[param_cnt].value  = &enc_props->enable_hw_buf;
    param_cnt++;

    return ENC_APP_SUCCESS;

}

static void xlnx_la_xma_params_update(XlnxLookaheadProperties *la_props, 
                                      XmaFilterProperties *xma_la_props)
{

    XmaParameter *extn_params = NULL;
    uint32_t param_cnt = 0;
    extn_params = (XmaParameter *)calloc(1, 
                  XLNX_LA_MAX_NUM_EXT_PARAMS * sizeof(XmaParameter));

    extn_params[param_cnt].name = 
                            (char *)XLNX_LA_EXT_PARAMS[EParamIntraPeriod];
    extn_params[param_cnt].user_type = EParamIntraPeriod;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->gop_size;
    param_cnt++;

    extn_params[param_cnt].name = (char *)XLNX_LA_EXT_PARAMS[EParamLADepth];
    extn_params[param_cnt].user_type = EParamLADepth;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->lookahead_depth;
    param_cnt++;

    extn_params[param_cnt].name = 
                            (char *)XLNX_LA_EXT_PARAMS[EParamEnableHwInBuf];
    extn_params[param_cnt].user_type = EParamEnableHwInBuf;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->enable_hw_buf;
    param_cnt++;

    extn_params[param_cnt].name = 
                            (char *)XLNX_LA_EXT_PARAMS[EParamSpatialAQMode];
    extn_params[param_cnt].user_type = EParamSpatialAQMode;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->spatial_aq_mode;
    param_cnt++;

    extn_params[param_cnt].name = 
        (char *)XLNX_LA_EXT_PARAMS[EParamTemporalAQMode];
    extn_params[param_cnt].user_type = EParamTemporalAQMode;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->temporal_aq_mode;
    param_cnt++;

    extn_params[param_cnt].name = 
        (char *)XLNX_LA_EXT_PARAMS[EParamRateControlMode];
    extn_params[param_cnt].user_type = EParamRateControlMode;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->rate_control_mode;
    param_cnt++;

    extn_params[param_cnt].name = 
                            (char *)XLNX_LA_EXT_PARAMS[EParamSpatialAQGain];
    extn_params[param_cnt].user_type = EParamSpatialAQGain;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->spatial_aq_gain;
    param_cnt++;

    extn_params[param_cnt].name = 
                            (char *)XLNX_LA_EXT_PARAMS[EParamNumBFrames];
    extn_params[param_cnt].user_type = EParamNumBFrames;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->num_bframes;
    param_cnt++;

    extn_params[param_cnt].name = (char *)XLNX_LA_EXT_PARAMS[EParamCodecType];
    extn_params[param_cnt].user_type = EParamCodecType;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->codec_type;
    param_cnt++;

    extn_params[param_cnt].name = 
        (char *)XLNX_LA_EXT_PARAMS[EParamLatencyLogging];
    extn_params[param_cnt].user_type = EParamLatencyLogging;
    extn_params[param_cnt].type = XMA_UINT32;
    extn_params[param_cnt].length = sizeof(int);
    extn_params[param_cnt].value = &la_props->latency_logging;
    param_cnt++;

    xma_la_props->param_cnt = param_cnt;
    xma_la_props->params = &extn_params[0];

    return;
}

int32_t xlnx_enc_get_xma_props(XlnxEncoderProperties *enc_props, 
                               XmaEncoderProperties *xma_enc_props)
{

    /* Initialize encoder properties */
    xma_enc_props->hwencoder_type = XMA_MULTI_ENCODER_TYPE;
    strcpy(xma_enc_props->hwvendor_string, "MPSoC");
    xma_enc_props->param_cnt = ENC_MAX_EXT_PARAMS;
    xma_enc_props->params = (XmaParameter *)calloc(1, 
                            xma_enc_props->param_cnt * sizeof(XmaParameter));

    xma_enc_props->format = XMA_VCU_NV12_FMT_TYPE;
    xma_enc_props->bits_per_pixel  = 8;
    xma_enc_props->width = enc_props->width;
    xma_enc_props->height = enc_props->height;
    xma_enc_props->rc_mode =  enc_props->custom_rc;

    switch(xma_enc_props->rc_mode) {
        case 0 : 
            break;

        case 1 : 
            if (enc_props->lookahead_depth < ENC_MIN_LOOKAHEAD_DEPTH ||
                    enc_props->lookahead_depth > ENC_MAX_LOOKAHEAD_DEPTH) {
                xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                        "Error: Provided LA Depth %d is invalid !\n", 
                        enc_props->lookahead_depth);
                xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                    "If RC mode is 1, the LA depth must lie between %d - %d\n",
                    ENC_MIN_LOOKAHEAD_DEPTH, ENC_MAX_LOOKAHEAD_DEPTH);
                return ENC_APP_FAILURE;
            } else {
                xma_enc_props->lookahead_depth = enc_props->lookahead_depth;
            }
            xma_logmsg(XMA_INFO_LOG, XLNX_ENC_APP_MODULE, 
                    "Encoder custom RC mode is enabled with LA depth = %d \n", 
                    xma_enc_props->lookahead_depth);
            break;

        default: 
            xma_enc_props->rc_mode = 0;
            xma_logmsg(XMA_INFO_LOG, XLNX_ENC_APP_MODULE, 
                    "Rate control mode is default\n");
            break;
    }

    xma_enc_props->framerate.numerator   = enc_props->fps;
    xma_enc_props->framerate.denominator = 1;

    /* Update encoder options */
    const char* RateCtrlMode = "CONST_QP";
    switch (enc_props->control_rate) {
        case 0: RateCtrlMode = "CONST_QP"; break;
        case 1: RateCtrlMode = "CBR"; break;
        case 2: RateCtrlMode = "VBR"; break;
        case 3: RateCtrlMode = "LOW_LATENCY"; break;
    }

    char FrameRate[16];
    int32_t fps_den = 1;
    sprintf(FrameRate, "%d/%d", enc_props->fps, fps_den);

    char SliceQP[8];
    if (enc_props->slice_qp == -1)
        strcpy (SliceQP, "AUTO");
    else
        sprintf(SliceQP, "%d", enc_props->slice_qp);

    const char* GopCtrlMode = "DEFAULT_GOP";
    switch (enc_props->gop_mode) {
        case 0: GopCtrlMode = "DEFAULT_GOP"; break;
        case 1: GopCtrlMode = "PYRAMIDAL_GOP"; break;
        case 2: GopCtrlMode = "LOW_DELAY_P"; break;
        case 3: GopCtrlMode = "LOW_DELAY_B"; break;
    }

    const char* GDRMode = "DISABLE";
    switch (enc_props->gdr_mode) {
        case 0: GDRMode = "DISABLE"; break;
        case 1: GDRMode = "GDR_VERTICAL"; break;
        case 2: GDRMode = "GDR_HORIZONTAL"; break;
    }

    const char* Profile = "AVC_BASELINE";
    if(enc_props->codec_id == ENCODER_ID_H264) {
        switch (enc_props->profile) {
            case ENC_H264_BASELINE: Profile = "AVC_BASELINE"; break;
            case ENC_H264_MAIN: Profile = "AVC_MAIN"; break;
            case ENC_H264_HIGH: Profile = "AVC_HIGH"; break;
            default:
                xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                    "Invalid H264 codec profile value %d \n", 
                    enc_props->profile);
                return ENC_APP_FAILURE;

        }
    } else if(enc_props->codec_id == ENCODER_ID_HEVC){
        Profile = "HEVC_MAIN";
        switch (enc_props->profile) {
            case ENC_HEVC_MAIN: Profile = "HEVC_MAIN"; break;
            case ENC_HEVC_MAIN_INTRA: Profile = "HEVC_MAIN_INTRA"; break;
            default:
                xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                    "Invalid HEVC codec profile value %d \n", 
                    enc_props->profile);
                return ENC_APP_FAILURE;
        }
    }

    const char* Level = "1";
    uint8_t is_level_found = 1;
    switch (enc_props->level) {
        case 10: Level = "1"; break;
        case 20: Level = "2"; break;
        case 21: Level = "2.1"; break;
        case 30: Level = "3"; break;
        case 31: Level = "3.1"; break;
        case 40: Level = "4"; break;
        case 41: Level = "4.1"; break;
        case 50: Level = "5"; break;
        case 51: Level = "5.1"; break;
        default:
            is_level_found = 0;
    }
    if (!is_level_found) {
        if(enc_props->codec_id == ENCODER_ID_H264) {
            switch (enc_props->level) {
                case 11: Level = "1.1"; break;
                case 12: Level = "1.2"; break;
                case 13: Level = "1.3"; break;
                case 22: Level = "2.2"; break;
                case 32: Level = "3.2"; break;
                case 42: Level = "4.2"; break;
                case 52: Level = "5.2"; break;
                default:
                    xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                            "Invalid H264 codec level value %d \n",
                            enc_props->level);
                    return ENC_APP_FAILURE;
            }
        } else if(enc_props->codec_id == ENCODER_ID_HEVC) {
            xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                       "Invalid HEVC codec level value %d \n",
                       enc_props->level);
            return ENC_APP_FAILURE;
        }
    }

    const char* Tier = "MAIN_TIER";
    switch (enc_props->tier) {
        case 0: Tier = "MAIN_TIER"; break;
        case 1: Tier = "HIGH_TIER"; break;
    }

    const char* QPCtrlMode = "UNIFORM_QP";
    switch (enc_props->qp_mode) {
        case 0: QPCtrlMode = "UNIFORM_QP"; break;
        case 1: QPCtrlMode = "AUTO_QP"; break;
        case 2: QPCtrlMode = "LOAD_QP | RELATIVE_QP"; break;
    }

    const char* DependentSlice = "FALSE";
    switch (enc_props->dependent_slice) {
        case 0: DependentSlice = "FALSE"; break;
        case 1: DependentSlice = "TRUE"; break;
    }

    const char* FillerData = "ENABLE";
    switch (enc_props->filler_data) {
        case 0: FillerData = "DISABLE"; break;
        case 1: FillerData = "ENABLE"; break;
    }

    const char* AspectRatio = "ASPECT_RATIO_AUTO";
    switch (enc_props->aspect_ratio) {
        case 0: AspectRatio = "ASPECT_RATIO_AUTO"; break;
        case 1: AspectRatio = "ASPECT_RATIO_4_3"; break;
        case 2: AspectRatio = "ASPECT_RATIO_16_9"; break;
        case 3: AspectRatio = "ASPECT_RATIO_NONE"; break;
    }

    const char* ColorSpace = "COLOUR_DESC_UNSPECIFIED";

    const char* ScalingList = "FLAT";
    switch (enc_props->scaling_list) {
        case 0: ScalingList = "FLAT"; break;
        case 1: ScalingList = "DEFAULT"; break;
    }

    const char* LoopFilter = "ENABLE";
    switch (enc_props->loop_filter) {
        case 0: LoopFilter = "DISABLE"; break;
        case 1: LoopFilter = "ENABLE"; break;
    }

    const char* EntropyMode = "MODE_CABAC";
    switch (enc_props->entropy_mode) {
        case 0: EntropyMode = "MODE_CAVLC"; break;
        case 1: EntropyMode = "MODE_CABAC"; break;
    }

    const char* ConstIntraPred = "ENABLE";
    switch (enc_props->constrained_intra_pred) {
        case 0: ConstIntraPred = "DISABLE"; break;
        case 1: ConstIntraPred = "ENABLE"; break;
    }

    const char* LambdaCtrlMode = "DEFAULT_LDA";

    const char* PrefetchBuffer = "DISABLE";
    switch (enc_props->prefetch_buffer) {
        case 0: PrefetchBuffer = "DISABLE"; break;
        case 1: PrefetchBuffer = "ENABLE"; break;
    }

    if (enc_props->tune_metrics){
        ScalingList = "FLAT";
        QPCtrlMode = "UNIFORM_QP";
    }

    if(enc_props->codec_id == ENCODER_ID_HEVC) {
        sprintf (enc_props->enc_options, "[INPUT]\n"
                "Width = %d\n"
                "Height = %d\n"
                "[RATE_CONTROL]\n"
                "RateCtrlMode = %s\n"
                "FrameRate = %s\n"
                "BitRate = %ld\n"
                "MaxBitRate = %ld\n"
                "SliceQP = %s\n"
                "MaxQP = %d\n"
                "MinQP = %d\n"
                "CPBSize = %f\n"
                "InitialDelay = %f\n"
                "[GOP]\n"
                "GopCtrlMode = %s\n"
                "Gop.GdrMode = %s\n"
                "Gop.Length = %d\n"
                "Gop.NumB = %d\n"
                "Gop.FreqIDR = %d\n"
                "[SETTINGS]\n"
                "Profile = %s\n"
                "Level = %s\n"
                "Tier = %s\n"
                "ChromaMode = CHROMA_4_2_0\n"
                "BitDepth = 8\n"
                "NumSlices = %d\n"
                "QPCtrlMode = %s\n"
                "SliceSize = %d\n"
                "DependentSlice = %s\n"
                "EnableFillerData = %s\n"
                "AspectRatio = %s\n"
                "ColourDescription = %s\n"
                "ScalingList = %s\n"
                "LoopFilter = %s\n"
                "ConstrainedIntraPred = %s\n"
                "LambdaCtrlMode = %s\n"
                "CacheLevel2 = %s\n"
                "NumCore = %d\n",
            enc_props->width, enc_props->height, RateCtrlMode, FrameRate, 
            enc_props->bit_rate, enc_props->max_bitrate, SliceQP, 
            enc_props->max_qp, enc_props->min_qp, enc_props->cpb_size,
            enc_props->initial_delay, GopCtrlMode, GDRMode, 
            enc_props->gop_size, enc_props->num_bframes, 
            enc_props->idr_period, Profile, Level, Tier, 
            enc_props->num_slices, QPCtrlMode, enc_props->slice_size, 
            DependentSlice, FillerData, AspectRatio, ColorSpace, 
            ScalingList, LoopFilter, ConstIntraPred, LambdaCtrlMode, 
            PrefetchBuffer, enc_props->num_cores);
    }
    else {
        sprintf (enc_props->enc_options, "[INPUT]\n"
                "Width = %d\n"
                "Height = %d\n"
                "[RATE_CONTROL]\n"
                "RateCtrlMode = %s\n"
                "FrameRate = %s\n"
                "BitRate = %ld\n"
                "MaxBitRate = %ld\n"
                "SliceQP = %s\n"
                "MaxQP = %d\n"
                "MinQP = %d\n"
                "CPBSize = %f\n"
                "InitialDelay = %f\n"
                "[GOP]\n"
                "GopCtrlMode = %s\n"
                "Gop.GdrMode = %s\n"
                "Gop.Length = %d\n"
                "Gop.NumB = %d\n"
                "Gop.FreqIDR = %d\n"
                "[SETTINGS]\n"
                "Profile = %s\n"
                "Level = %s\n"
                "ChromaMode = CHROMA_4_2_0\n"
                "BitDepth = 8\n"
                "NumSlices = %d\n"
                "QPCtrlMode = %s\n"
                "SliceSize = %d\n"
                "EnableFillerData = %s\n"
                "AspectRatio = %s\n"
                "ColourDescription = %s\n"
                "ScalingList = %s\n"
                "EntropyMode = %s\n"
                "LoopFilter = %s\n"
                "ConstrainedIntraPred = %s\n"
                "LambdaCtrlMode = %s\n"
                "CacheLevel2 = %s\n"
                "NumCore = %d\n",
            enc_props->width, enc_props->height, RateCtrlMode, FrameRate, 
            enc_props->bit_rate, enc_props->max_bitrate, SliceQP, 
            enc_props->max_qp, enc_props->min_qp, enc_props->cpb_size,
            enc_props->initial_delay, GopCtrlMode, GDRMode, 
            enc_props->gop_size, enc_props->num_bframes, 
            enc_props->idr_period, Profile, Level, enc_props->num_slices,
            QPCtrlMode, enc_props->slice_size, FillerData, AspectRatio, 
            ColorSpace, ScalingList, EntropyMode, LoopFilter, 
            ConstIntraPred, LambdaCtrlMode, PrefetchBuffer, 
            enc_props->num_cores);
    }

    xlnx_enc_xma_params_update(enc_props, xma_enc_props);

    return ENC_APP_SUCCESS;
}

void xlnx_la_get_xma_props(XlnxLookaheadProperties *la_props, 
                           XmaFilterProperties *xma_la_props)
{

    XmaFilterPortProperties *in_props;
    XmaFilterPortProperties *out_props;

    /* Setup lookahead properties */
    memset(xma_la_props, 0, sizeof(XmaFilterProperties));
    xma_la_props->hwfilter_type = XMA_2D_FILTER_TYPE;
    strcpy(xma_la_props->hwvendor_string, "Xilinx");

    /* Setup lookahead input port properties */
    in_props = &xma_la_props->input;
    memset(in_props, 0, sizeof(XmaFilterPortProperties));
    in_props->format = la_props->xma_fmt_type;
    in_props->bits_per_pixel = la_props->bits_per_pixel;
    in_props->width = la_props->width;
    in_props->height = la_props->height;
    in_props->stride = la_props->stride;
    in_props->framerate.numerator = la_props->framerate.numerator;
    in_props->framerate.denominator = la_props->framerate.denominator;

    /* Setup lookahead output port properties */
    out_props = &xma_la_props->output;
    memset(out_props, 0, sizeof(XmaFilterPortProperties));
    out_props->format = la_props->xma_fmt_type;
    out_props->bits_per_pixel = la_props->bits_per_pixel;
    out_props->width = 
                   XLNX_ENC_LINE_ALIGN((in_props->width), 64) >> XLNX_SCLEVEL1;
    out_props->height = 
                  XLNX_ENC_LINE_ALIGN((in_props->height), 64) >> XLNX_SCLEVEL1;
    out_props->framerate.numerator = la_props->framerate.numerator;
    out_props->framerate.denominator = la_props->framerate.denominator;

    xlnx_la_xma_params_update(la_props, xma_la_props);
    return;
}

void xlnx_enc_free_xma_props(XmaEncoderProperties *xma_enc_props)
{
    if(xma_enc_props->params)
        free(xma_enc_props->params);

    return;
}

void xlnx_la_free_xma_props(XmaFilterProperties *xma_la_props)
{
    if(xma_la_props->params)
        free(xma_la_props->params);

    return;
}

static int32_t xlnx_enc_fill_pool_props(xrmCuPoolProperty *enc_cu_pool_prop, 
                                        XlnxEncoderXrmCtx *enc_xrm_ctx)
{
    int32_t cu_num = 0;
    enc_cu_pool_prop->cuListProp.sameDevice = true;
    enc_cu_pool_prop->cuListNum = 1;

    if (enc_xrm_ctx->enc_load > 0) {
        strcpy(enc_cu_pool_prop->cuListProp.cuProps[cu_num].kernelName, 
               "encoder");
        strcpy(enc_cu_pool_prop->cuListProp.cuProps[cu_num].kernelAlias, 
               "ENCODER_MPSOC");
        enc_cu_pool_prop->cuListProp.cuProps[cu_num].devExcl = false;
        enc_cu_pool_prop->cuListProp.cuProps[cu_num].requestLoad = 
                                                     XRM_PRECISION_1000000_BIT_MASK(enc_xrm_ctx->enc_load);
        cu_num++;

        for(int32_t i = 0; i < enc_xrm_ctx->enc_num; i++) {
            strcpy(enc_cu_pool_prop->cuListProp.cuProps[cu_num].kernelName, 
                   "kernel_vcu_encoder");
            strcpy(enc_cu_pool_prop->cuListProp.cuProps[cu_num].kernelAlias, 
                   "");
            enc_cu_pool_prop->cuListProp.cuProps[cu_num].devExcl = false;
            enc_cu_pool_prop->cuListProp.cuProps[cu_num].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
            cu_num++;
        }
    }

    if(enc_xrm_ctx->la_load > 0) {
        strcpy(enc_cu_pool_prop->cuListProp.cuProps[cu_num].kernelName, 
                "lookahead");
        strcpy(enc_cu_pool_prop->cuListProp.cuProps[cu_num].kernelAlias, 
                "LOOKAHEAD_MPSOC");
        enc_cu_pool_prop->cuListProp.cuProps[cu_num].devExcl = false;
        enc_cu_pool_prop->cuListProp.cuProps[cu_num].requestLoad = 
                                                     XRM_PRECISION_1000000_BIT_MASK(enc_xrm_ctx->la_load);
        cu_num++;

    }

    enc_cu_pool_prop->cuListProp.cuNum = cu_num;
    return ENC_APP_SUCCESS;

}

static int32_t xlnx_la_load_calc(XlnxEncoderXrmCtx *enc_xrm_ctx, 
                                 XmaEncoderProperties *xma_enc_props)
{

    int32_t la_load = 0;
    int32_t skip_value = 0;
    int32_t func_id = 0;
    char pluginName[XRM_MAX_NAME_LEN];
    XmaFilterProperties filter_props;
    xrmPluginFuncParam plg_param;

    /* Update the lookahead props that are needed for libxmaPropsTOjson */
    filter_props.input.width = xma_enc_props->width;
    filter_props.input.height = xma_enc_props->height;
    filter_props.input.framerate.numerator = xma_enc_props->framerate.numerator;
    filter_props.input.framerate.denominator = 
                                 xma_enc_props->framerate.denominator;

    memset(&plg_param, 0, sizeof(xrmPluginFuncParam));
    void *handle;
    void (*convertXmaPropsToJson)(void* props, char* funcName, char* jsonJob);

    /* Loading propstojson library to convert LA properties to json */
    handle = dlopen("/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so", RTLD_NOW );

    convertXmaPropsToJson = dlsym(handle, "convertXmaPropsToJson");
    (*convertXmaPropsToJson) (&filter_props, "LOOKAHEAD", plg_param.input);
    dlclose(handle);

    strcpy(pluginName, "xrmU30EncPlugin");

    if (xrmExecPluginFunc(enc_xrm_ctx->xrm_ctx, pluginName, func_id, &plg_param) 
                          != XRM_SUCCESS) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                   "XRM LA plugin failed \n");
        return ENC_APP_FAILURE;
    }
    else
    {
        skip_value = atoi((char *)(strtok(plg_param.output, " ")));
        skip_value = atoi((char *)(strtok(NULL, " ")));
        /* To silence the warning of skip_value set, but not used */
        (void)skip_value;
        la_load = atoi((char *)(strtok(NULL, " ")));
    }
    return la_load;

}

static int32_t xlnx_enc_load_calc(XlnxEncoderXrmCtx *enc_xrm_ctx,
                                  XmaEncoderProperties *xma_enc_props, 
                                  int32_t lookahead_enable,
                                  xrmCuPoolProperty *enc_cu_pool_prop)
{

    int32_t func_id = 0;
    char pluginName[XRM_MAX_NAME_LEN];
    xrmPluginFuncParam plg_param;

    memset(&plg_param, 0, sizeof(xrmPluginFuncParam));
    void *handle;
    void (*convertXmaPropsToJson)(void* props, char* funcName, char* jsonJob);

    /* Loading propstojson library to convert encoder properties to json */
    handle = dlopen("/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so", RTLD_NOW );

    convertXmaPropsToJson = dlsym(handle, "convertXmaPropsToJson");
    (*convertXmaPropsToJson) (xma_enc_props, "ENCODER", plg_param.input);
    dlclose(handle);

    strcpy(pluginName, "xrmU30EncPlugin");

    if (xrmExecPluginFunc(enc_xrm_ctx->xrm_ctx, pluginName, func_id, 
                          &plg_param) != XRM_SUCCESS) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                "XRM encoder plugin failed \n");
        return ENC_APP_FAILURE;
    }
    else {
        enc_xrm_ctx->enc_load = atoi((char*)(strtok(plg_param.output, " ")));
        enc_xrm_ctx->enc_num = atoi((char*)(strtok(NULL, " ")));
    }

    /* If LA is enabled, calculate the load to reserve the CU*/
    if(lookahead_enable) {
        enc_xrm_ctx->la_load = xlnx_la_load_calc(enc_xrm_ctx, xma_enc_props);
        if(enc_xrm_ctx->la_load <= 0) {
            xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                    "Lookahead XRM load calculation failed \n");
            return ENC_APP_FAILURE;
        }
    }
    xlnx_enc_fill_pool_props(enc_cu_pool_prop, enc_xrm_ctx);

    return ENC_APP_SUCCESS;

}

int32_t xlnx_enc_device_init(XlnxEncoderXrmCtx *enc_xrm_ctx,
                        XmaEncoderProperties *xma_enc_props,
                        int32_t lookahead_enable)
{

    xrmCuPoolProperty enc_cu_pool_prop;
    int32_t ret = ENC_APP_FAILURE;
    int32_t num_cu_pool = 0;

    memset(&enc_cu_pool_prop, 0, sizeof(enc_cu_pool_prop));

    /* Create XRM context */
    enc_xrm_ctx->xrm_ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1);
    if (enc_xrm_ctx->xrm_ctx == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                "create local XRM context failed\n");
        return ret;
    }

    /* Calculate encoder load based on encoder properties */
    ret = xlnx_enc_load_calc(enc_xrm_ctx, xma_enc_props, lookahead_enable, 
                             &enc_cu_pool_prop);
    if(ret != ENC_APP_SUCCESS) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                "Enc load calculation failed %d \n", ret);
        return ret;
    }

    /* Check the number of pools available for the given encoder load */
    num_cu_pool = xrmCheckCuPoolAvailableNum(enc_xrm_ctx->xrm_ctx, 
            &enc_cu_pool_prop);
    if(num_cu_pool <= 0) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                "No resources available for allocation \n");
        return ENC_APP_FAILURE;
    }

    /* If the device reservation ID is not sent through command line, get the
       next available device id */
    if(enc_xrm_ctx->device_id < 0) {

        /* Query XRM to get reservation index for the required CU */
        enc_xrm_ctx->enc_res_idx = xrmCuPoolReserve(enc_xrm_ctx->xrm_ctx, 
                &enc_cu_pool_prop);
        if (enc_xrm_ctx->enc_res_idx == 0) {
            xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                    "Failed to reserve encode cu pool\n");
            return ENC_APP_FAILURE;
        }
        xrmCuPoolResource enc_cu_pool_res;
        memset(&enc_cu_pool_res, 0, sizeof(enc_cu_pool_res));

        /* Query XRM for the CU pool resource details like xclbinname and 
           device ID */
        ret = xrmReservationQuery(enc_xrm_ctx->xrm_ctx, 
              enc_xrm_ctx->enc_res_idx, &enc_cu_pool_res);
        if (ret != 0) {
            xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                    "Failed to query reserved  cu list\n");
            return ENC_APP_FAILURE;
        }

        /* xclbin configuration */
        XmaXclbinParameter xclbin_param;
        xclbin_param.device_id = enc_cu_pool_res.cuResources[0].deviceId;
        xclbin_param.xclbin_name = 
                                 enc_cu_pool_res.cuResources[0].xclbinFileName; 

        if ((ret = xma_initialize(&xclbin_param, 1)) != XMA_SUCCESS)
        {
            xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                    "XMA Initialization failed\n");
            return ENC_APP_FAILURE;
        }
        xma_logmsg(XMA_NOTICE_LOG, XLNX_ENC_APP_MODULE, 
                "XMA initialization success \n");
    }
    else {
        /* xclbin configuration */
        XmaXclbinParameter xclbin_param;
        xclbin_param.device_id = enc_xrm_ctx->device_id;
        xclbin_param.xclbin_name = "/opt/xilinx/xcdr/xclbins/transcode.xclbin"; 

        xma_logmsg(XMA_INFO_LOG, XLNX_ENC_APP_MODULE, 
            "Device ID %d selected to run encoder \n", enc_xrm_ctx->device_id);
        if ((ret = xma_initialize(&xclbin_param, 1)) != XMA_SUCCESS)
        {
            xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                    "XMA Initialization failed\n");
            return ENC_APP_FAILURE;
        }
        else {
            xma_logmsg(XMA_INFO_LOG, XLNX_ENC_APP_MODULE, 
                    "XMA initialization success \n");
        }

    }

    return ENC_APP_SUCCESS;

}

void xlnx_enc_xrm_deinit(XlnxEncoderXrmCtx *enc_xrm_ctx)
{

    if(enc_xrm_ctx->enc_res_in_use) {
        xrmCuListRelease(enc_xrm_ctx->xrm_ctx, 
                         &enc_xrm_ctx->encode_cu_list_res);
    }

    if((enc_xrm_ctx->device_id < 0) && (enc_xrm_ctx->enc_res_idx >= 0)) {
        xrmCuPoolRelinquish(enc_xrm_ctx->xrm_ctx, enc_xrm_ctx->enc_res_idx);
    }

    if (enc_xrm_ctx->lookahead_res_inuse ==1) {
        xrmCuRelease(enc_xrm_ctx->xrm_ctx, &enc_xrm_ctx->lookahead_cu_res);
    }

    xrmDestroyContext(enc_xrm_ctx->xrm_ctx);
    return;
}

static void xlnx_enc_app_close(XlnxEncoderCtx *enc_ctx, 
                          XmaEncoderProperties *xma_enc_props,
                          XmaFilterProperties  *xma_la_props)
{
    xlnx_enc_deinit(enc_ctx, xma_enc_props);
    xlnx_la_deinit(&enc_ctx->la_ctx, xma_la_props);
    xlnx_enc_xrm_deinit(&enc_ctx->enc_xrm_ctx);

    return;
}

static int32_t xlnx_la_get_num_planes(XmaFormatType format)
{
    /* multi scaler supports max 2 planes till v2019.1 */
    switch (format) {
        case XMA_RGB888_FMT_TYPE: /* BGR */
            return 1;
        case XMA_YUV420_FMT_TYPE: /* NV12 */
            return 2;
        case XMA_VCU_NV12_FMT_TYPE: /* VCU_NV12 */
            return 1;
        default:
            xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                       "Unsupported format...");
            return -1;
    }
}

static int32_t xlnx_la_allocate_xrm_cu(XlnxLookaheadCtx *la_ctx, XlnxEncoderXrmCtx *enc_xrm_ctx,XmaFilterProperties *xma_la_props)
{

    int32_t ret = ENC_APP_FAILURE;
    /* XRM lookahead allocation */
    xrmCuProperty lookahead_cu_prop;

    memset(&lookahead_cu_prop, 0, sizeof(xrmCuProperty));
    memset(&enc_xrm_ctx->lookahead_cu_res, 0, sizeof(xrmCuResource));

    strcpy(lookahead_cu_prop.kernelName, "lookahead");
    strcpy(lookahead_cu_prop.kernelAlias, "LOOKAHEAD_MPSOC");
    lookahead_cu_prop.devExcl = false;
    lookahead_cu_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK(enc_xrm_ctx->la_load);

    if(enc_xrm_ctx->device_id < 0) {
        lookahead_cu_prop.poolId = enc_xrm_ctx->enc_res_idx;
        ret = xrmCuAlloc(enc_xrm_ctx->xrm_ctx, &lookahead_cu_prop,
                         &enc_xrm_ctx->lookahead_cu_res);
    }
    else {
        ret = xrmCuAllocFromDev(enc_xrm_ctx->xrm_ctx,  enc_xrm_ctx->device_id,
                          &lookahead_cu_prop, &enc_xrm_ctx->lookahead_cu_res);
    }

    if (ret != 0) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                "xrm_allocation: fail to allocate lookahead cu \n");
        return ret;
    } else {
        enc_xrm_ctx->lookahead_res_inuse = 1;
    }

    /* Set XMA plugin SO and device index */
    xma_la_props->plugin_lib = 
                         enc_xrm_ctx->lookahead_cu_res.kernelPluginFileName;
    xma_la_props->dev_index = enc_xrm_ctx->lookahead_cu_res.deviceId;
    /* XMA to select the ddr bank based on xclbin meta data */
    xma_la_props->ddr_bank_index = -1;
    xma_la_props->cu_index = enc_xrm_ctx->lookahead_cu_res.cuId;
    xma_la_props->channel_id = enc_xrm_ctx->lookahead_cu_res.channelId;

    return ret;
}

int32_t xlnx_la_create(XlnxLookaheadCtx *la_ctx, XlnxEncoderXrmCtx *enc_xrm_ctx, XmaFilterProperties *xma_la_props)
{

    XlnxLookaheadProperties *la_props = &la_ctx->la_props;

    if (!la_ctx) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                "No LA context received\n");
        return ENC_APP_FAILURE;
    }
    if ((la_props->lookahead_depth == 0) && 
        (la_props->temporal_aq_mode == 1)) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                "Invalid params: Lookahead = 0, temporal aq=%u\n",
                la_props->temporal_aq_mode);
        return ENC_APP_SUCCESS;
    }

    if (((la_props->lookahead_depth == 0) && (la_props->spatial_aq_mode == 0))
    || ((la_props->spatial_aq_mode == 0) && (la_props->temporal_aq_mode == 0) 
    && (la_props->rate_control_mode == 0))) {
        la_ctx->bypass = 1;
        return ENC_APP_SUCCESS;
    }

    la_ctx->num_planes = xlnx_la_get_num_planes(la_ctx->la_props.xma_fmt_type);
    la_ctx->bypass = 0;

    xlnx_la_get_xma_props(&la_ctx->la_props, xma_la_props);

    enc_xrm_ctx->lookahead_res_inuse = 0;
    xlnx_la_allocate_xrm_cu(la_ctx, enc_xrm_ctx, xma_la_props);

    /* Create lookahead session based on the requested properties */
    la_ctx->filter_session = xma_filter_session_create(xma_la_props);
    if (!la_ctx->filter_session) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                "Failed to create lookahead session\n");
        return ENC_APP_FAILURE;
    }

    la_ctx->xma_la_frame = (XmaFrame *) calloc(1, sizeof(XmaFrame));
    if (la_ctx->xma_la_frame == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                "Out of memory while allocating la out frame. \n");
        return ENC_APP_FAILURE;
    }

    return ENC_APP_SUCCESS;
}

int32_t xlnx_la_get_bypass_mode(XlnxLookaheadCtx *la_ctx)
{
    if (!la_ctx) {
        return ENC_APP_FAILURE;
    }
    return la_ctx->bypass;
}

int32_t xlnx_enc_la_init(XlnxEncoderCtx *enc_ctx, 
                         XmaFilterProperties  *xma_la_props)
{
    int32_t ret = ENC_APP_FAILURE;
    XlnxLookaheadProperties *la_props = &enc_ctx->la_ctx.la_props;
    XlnxEncoderProperties *enc_props = &enc_ctx->enc_props;

    la_props->width = enc_props->width;
    la_props->height = enc_props->height;
    la_props->framerate.numerator = enc_props->fps;
    la_props->framerate.denominator = 1;

    //TODO: Assume 256 aligned for now. Needs to be fixed later
    la_props->stride = XLNX_ENC_LINE_ALIGN(enc_props->width, VCU_STRIDE_ALIGN);
    la_props->bits_per_pixel = 8;

    if (enc_props->gop_size <= 0) {
        la_props->gop_size = ENC_DEFAULT_GOP_SIZE;
    } else {
        la_props->gop_size = enc_props->gop_size;
    }

    la_props->lookahead_depth = enc_props->lookahead_depth;
    la_props->spatial_aq_mode = enc_props->spatial_aq;
    la_props->spatial_aq_gain = enc_props->spatial_aq_gain;
    la_props->temporal_aq_mode = enc_props->temporal_aq;
    la_props->rate_control_mode = enc_props->custom_rc;
    la_props->num_bframes = enc_props->num_bframes;
    la_props->latency_logging = enc_props->latency_logging;

    /* Only NV12 format is supported in this application */
    la_props->xma_fmt_type = XMA_VCU_NV12_FMT_TYPE;
    la_props->enable_hw_buf = 0;

    switch (enc_props->codec_id) {
        case ENCODER_ID_H264:
            la_props->codec_type = LOOKAHEAD_ID_H264;
            break;
        case ENCODER_ID_HEVC:
            la_props->codec_type = LOOKAHEAD_ID_HEVC;
            break;
    }

    ret = xlnx_la_create(&enc_ctx->la_ctx, &enc_ctx->enc_xrm_ctx, xma_la_props);
    if (ret != ENC_APP_SUCCESS) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                "Error : init_la : create_xlnx_la Failed \n");
        return ENC_APP_FAILURE;
    }

    enc_ctx->la_bypass = xlnx_la_get_bypass_mode(&enc_ctx->la_ctx);

    return ENC_APP_SUCCESS;
}

static void xlnx_enc_context_init(XlnxEncoderCtx *enc_ctx)
{

    XlnxEncoderProperties *enc_props = &enc_ctx->enc_props;

    /* Initialize the encoder parameters to default */
    enc_ctx->enc_xrm_ctx.device_id = -1;
    enc_ctx->enc_xrm_ctx.enc_res_idx = -1;
    enc_ctx->enc_xrm_ctx.enc_res_in_use = 0;
    enc_ctx->enc_xrm_ctx.lookahead_res_inuse = 0;

    enc_ctx->loop_count = 0;
    enc_ctx->num_frames = SIZE_MAX;
    enc_props->codec_id = -1;
    enc_props->width = ENC_DEFAULT_WIDTH;
    enc_props->height = ENC_DEFAULT_HEIGHT;
    enc_props->bit_rate = ENC_DEFAULT_BITRATE;
    enc_props->fps = ENC_DEFAULT_FRAMERATE;
    enc_props->gop_size = ENC_DEFAULT_GOP_SIZE;
    enc_props->slice_qp = -1;
    enc_props->control_rate = 1;
    enc_props->custom_rc = 0;
    enc_props->max_bitrate = ENC_DEFAULT_MAX_BITRATE;
    enc_props->min_qp = 0;
    enc_props->max_qp = ENC_SUPPORTED_MAX_QP;
    enc_props->cpb_size = 2.0;
    enc_props->initial_delay = 1.0;
    enc_props->gop_mode = 0;
    enc_props->gdr_mode = 0;
    enc_props->num_bframes = ENC_DEFAULT_NUM_B_FRAMES;
    enc_props->idr_period = -1;

    /* Assigning the default profile as HEVC profile. If the codec option 
       is H264, this will be updated */
    enc_props->profile = ENC_HEVC_MAIN;
    enc_props->level = ENC_DEFAULT_LEVEL;
    enc_props->tier = 0;
    enc_props->num_slices = 1;
    enc_props->qp_mode = 1;
    enc_props->aspect_ratio = 0;
    enc_props->lookahead_depth = 0;
    enc_props->temporal_aq = 1;
    enc_props->spatial_aq = 1;
    enc_props->spatial_aq_gain = ENC_DEFAULT_SPAT_AQ_GAIN;
    enc_props->scaling_list = 1;
    enc_props->filler_data = 0;
    enc_props->dependent_slice = 0;
    enc_props->slice_size = 0;
    enc_props->entropy_mode = 1;
    enc_props->loop_filter = 1;
    enc_props->constrained_intra_pred = 0;
    enc_props->prefetch_buffer = 1;
    enc_props->latency_logging = 0;
    enc_props->enable_hw_buf = 1;
    enc_props->num_cores = 0;
    enc_props->tune_metrics = 0;

    enc_ctx->pts = 0;
    enc_ctx->out_frame_cnt = 0;
    enc_ctx->in_frame_cnt = 0;
    enc_ctx->enc_state = ENC_READ_INPUT;
    enc_ctx->la_in_frame = &(enc_ctx->in_frame);
    enc_ctx->enc_in_frame = &(enc_ctx->in_frame);

}

static int32_t xlnx_enc_update_props(XlnxEncoderCtx *enc_ctx, 
                                     XmaEncoderProperties *xma_enc_props)
{

    XlnxEncoderProperties *enc_props = &enc_ctx->enc_props;
    enc_props->enc_options = calloc(1, ENC_MAX_OPTIONS_SIZE);

    /* Enable custom rate control when rate control is set to CBR and 
    lookahead is set, disable when expert option lookahead-rc-off is set. */
    if((enc_props->control_rate == 1) && (enc_props->lookahead_depth > 1)) {
        enc_props->custom_rc = 1;
    }

    /* Enable Adaptive Quantization by default, if lookahead is enabled */
    if (enc_props->lookahead_depth >= 1 && (enc_props->temporal_aq == 1 || 
                enc_props->spatial_aq == 1) && (enc_props->tune_metrics == 0)) {
        xma_logmsg(XMA_INFO_LOG, XLNX_ENC_APP_MODULE, 
                "Setting qp mode to 2, as the lookahead params are set \n");
        enc_props->qp_mode = 2;
    }
    else if ((enc_props->lookahead_depth == 0) || 
            (enc_props->tune_metrics == 1)) {
        if (enc_props->temporal_aq)
            enc_props->temporal_aq = 0;

        if (enc_props->spatial_aq)
            enc_props->spatial_aq = 0;
    }

    /* Tunes video quality for objective scores by setting flat scaling-list 
       and uniform qp-mode */
    if (enc_props->tune_metrics){
        enc_props->scaling_list = 0;
        enc_props->qp_mode = 0;
    }

    /* Enable Adaptive Quantization by default, if lookahead is enabled */
    if (enc_props->lookahead_depth >= 1 && (enc_props->temporal_aq == 1 || 
        enc_props->spatial_aq == 1)) {
        xma_logmsg(XMA_INFO_LOG, XLNX_ENC_APP_MODULE, 
                "Setting qp mode to 2, as the lookahead params are set \n");
        enc_props->qp_mode = 2;
    }
    else if (enc_props->lookahead_depth == 0) {
        if (enc_props->temporal_aq)
            enc_props->temporal_aq = 0;

        if (enc_props->spatial_aq)
            enc_props->spatial_aq = 0;

        enc_props->enable_hw_buf = 0;
    }

    /* Set IDR period to gop-size, when the user has not specified it on 
       the command line */
    if (enc_props->idr_period == -1)
    {
        if (enc_props->gop_size > 0){
            enc_props->idr_period = enc_props->gop_size;
        }
        xma_logmsg(XMA_INFO_LOG, XLNX_ENC_APP_MODULE, 
                "Setting IDR period to GOP size \n");
    }

    return xlnx_enc_get_xma_props(enc_props, xma_enc_props);
}

static void xlnx_enc_frame_init(XlnxEncoderCtx *enc_ctx)
{

    XmaFrameProperties *frame_props = &(enc_ctx->in_frame.frame_props);
    frame_props->format = XMA_VCU_NV12_FMT_TYPE;
    frame_props->width  = enc_ctx->enc_props.width;
    frame_props->height = enc_ctx->enc_props.height;
    frame_props->linesize[0] = enc_ctx->enc_props.width;
    frame_props->linesize[1] = enc_ctx->enc_props.width;
    frame_props->bits_per_pixel = 8;

    return;
}

static int32_t xlnx_xlnx_enc_cu_alloc_device_id(XlnxEncoderXrmCtx *enc_xrm_ctx, 
                                        XmaEncoderProperties *xma_enc_props)
{

    xrmCuProperty encode_cu_hw_prop, encode_cu_sw_prop;

    int32_t ret = -1;

    memset(&encode_cu_hw_prop, 0, sizeof(xrmCuProperty));
    memset(&encode_cu_sw_prop, 0, sizeof(xrmCuProperty));
    memset(&enc_xrm_ctx->encode_cu_list_res, 0, sizeof(xrmCuListResource));

    strcpy(encode_cu_hw_prop.kernelName, "encoder");
    strcpy(encode_cu_hw_prop.kernelAlias, "ENCODER_MPSOC");
    encode_cu_hw_prop.devExcl = false;
    encode_cu_hw_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK(enc_xrm_ctx->enc_load);

    strcpy(encode_cu_sw_prop.kernelName, "kernel_vcu_encoder");
    encode_cu_sw_prop.devExcl = false;
    encode_cu_sw_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);

    ret = xrmCuAllocFromDev(enc_xrm_ctx->xrm_ctx, enc_xrm_ctx->device_id, 
            &encode_cu_hw_prop, &enc_xrm_ctx->encode_cu_list_res.cuResources[0]);

    if (ret <= ENC_APP_FAILURE)
    {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                   "xrm failed to allocate encoder resources on device %d\n", 
                   enc_xrm_ctx->device_id);
        return ret;
    }
    else
    {
        ret = xrmCuAllocFromDev(enc_xrm_ctx->xrm_ctx, enc_xrm_ctx->device_id, 
                &encode_cu_sw_prop, &enc_xrm_ctx->encode_cu_list_res.cuResources[1]);
        if (ret <= ENC_APP_FAILURE)
        {
            xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                    "xrm failed to allocate encoder resources on device %d\n", 
                    enc_xrm_ctx->device_id);
            return ret;
        }
    }

    /* Set XMA plugin SO and device index */
    xma_enc_props->plugin_lib = 
        enc_xrm_ctx->encode_cu_list_res.cuResources[0].kernelPluginFileName;
    xma_enc_props->dev_index = 
        enc_xrm_ctx->encode_cu_list_res.cuResources[0].deviceId;
    /* XMA to select the ddr bank based on xclbin meta data */
    xma_enc_props->ddr_bank_index = -1;
    xma_enc_props->cu_index = 
        enc_xrm_ctx->encode_cu_list_res.cuResources[1].cuId;
    xma_enc_props->channel_id = 
        enc_xrm_ctx->encode_cu_list_res.cuResources[1].channelId;

    enc_xrm_ctx->enc_res_in_use = 1;

    return ret;
}

static int32_t xlnx_xlnx_enc_cu_alloc_reserve_id(XlnxEncoderXrmCtx *enc_xrm_ctx, 
                                        XmaEncoderProperties *xma_enc_props)
{

    int32_t ret = ENC_APP_FAILURE;

    /* XRM encoder allocation */
    xrmCuListProperty encode_cu_list_prop;

    memset(&encode_cu_list_prop, 0, sizeof(xrmCuListProperty));
    memset(&enc_xrm_ctx->encode_cu_list_res, 0, sizeof(xrmCuListResource));

    encode_cu_list_prop.cuNum = 2;
    strcpy(encode_cu_list_prop.cuProps[0].kernelName, "encoder");
    strcpy(encode_cu_list_prop.cuProps[0].kernelAlias, "ENCODER_MPSOC");
    encode_cu_list_prop.cuProps[0].devExcl = false;
    encode_cu_list_prop.cuProps[0].requestLoad = XRM_PRECISION_1000000_BIT_MASK(enc_xrm_ctx->enc_load);
    encode_cu_list_prop.cuProps[0].poolId = enc_xrm_ctx->enc_res_idx;

    strcpy(encode_cu_list_prop.cuProps[1].kernelName, "kernel_vcu_encoder");
    encode_cu_list_prop.cuProps[1].devExcl = false;
    encode_cu_list_prop.cuProps[1].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
    encode_cu_list_prop.cuProps[1].poolId = enc_xrm_ctx->enc_res_idx;

    ret = xrmCuListAlloc(enc_xrm_ctx->xrm_ctx, &encode_cu_list_prop, 
            &enc_xrm_ctx->encode_cu_list_res);
    if (ret != ENC_APP_SUCCESS) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                "Failed to allocate encoder cu from reserve id \n");
        return ret;
    }

    /* Set XMA plugin SO and device index */
    xma_enc_props->plugin_lib = 
        enc_xrm_ctx->encode_cu_list_res.cuResources[0].kernelPluginFileName;
    xma_enc_props->dev_index = 
        enc_xrm_ctx->encode_cu_list_res.cuResources[0].deviceId;
    /* XMA to select the ddr bank based on xclbin meta data */
    xma_enc_props->ddr_bank_index = -1;
    xma_enc_props->cu_index = 
        enc_xrm_ctx->encode_cu_list_res.cuResources[1].cuId;
    xma_enc_props->channel_id = 
        enc_xrm_ctx->encode_cu_list_res.cuResources[1].channelId;

    enc_xrm_ctx->enc_res_in_use = 1;

    return ret;
}

static int32_t xlnx_enc_cu_alloc(XlnxEncoderXrmCtx *enc_xrm_ctx, 
                                 XmaEncoderProperties *xma_enc_props)
{

    int32_t ret = ENC_APP_FAILURE;

    if(enc_xrm_ctx->device_id >= 0) {
        ret = xlnx_xlnx_enc_cu_alloc_device_id(enc_xrm_ctx, xma_enc_props);
    }
    else {
        ret = xlnx_xlnx_enc_cu_alloc_reserve_id(enc_xrm_ctx, xma_enc_props);
    }

    return ret;
}

int32_t xlnx_enc_create_session(XlnxEncoderCtx *enc_ctx, 
                                XmaEncoderProperties *xma_enc_props)
{

    XlnxEncoderXrmCtx *enc_xrm_ctx = &enc_ctx->enc_xrm_ctx;
    if(xlnx_enc_cu_alloc(enc_xrm_ctx, xma_enc_props) != ENC_APP_SUCCESS) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                "Error in encoder CU allocation \n");
        return ENC_APP_FAILURE;
    }

    /* Encoder session creation */
    enc_ctx->enc_session = xma_enc_session_create(xma_enc_props);
    if(enc_ctx->enc_session == NULL) {
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                "Encoder session creation failed \n");
        return ENC_APP_FAILURE;
    }

    #ifdef U30V2
    XmaDataBuffer* output_xma_buffer = &enc_ctx->xma_buffer;
    /* Allocate enough data to safely recv */
    output_xma_buffer->alloc_size  = (3 * enc_ctx->enc_props.width *
                            enc_ctx->enc_props.height) >> 1;
    output_xma_buffer->data.buffer = malloc(output_xma_buffer->alloc_size);
    if(!output_xma_buffer->data.buffer) {
         xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE,
                    "Encoder failed to allocate data buffer for recv! \n");
    }
    #endif

    return ENC_APP_SUCCESS;
}

static int32_t xlnx_la_send_frame(XlnxLookaheadCtx *la_ctx, XmaFrame *in_frame)
{

    int32_t rc;
    if (!la_ctx) 
	{
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, "xlnx_la_send_frame : XMA_ERROR\n");
        return ENC_APP_FAILURE;
    }
    if (in_frame && in_frame->do_not_encode) 
	{
        rc = ENC_APP_SUCCESS;
    } 
	else 
	{
        rc = xma_filter_session_send_frame(la_ctx->filter_session,
                in_frame);
    }
    if (rc <= XMA_ERROR) 
	{
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, "xlnx_la_send_frame : Send frame to LA xma plg Failed!!\n");
        rc = ENC_APP_FAILURE;
    }
    return rc;
}

int32_t xlnx_la_process_frame(XlnxLookaheadCtx *la_ctx, XmaFrame *in_frame,XmaFrame **out_frame)
{
    int32_t ret = 0;
    if (out_frame == NULL) 
	{
        return XMA_ERROR;
    }
    if (la_ctx->bypass == 1) 
	{
        *out_frame = in_frame;
        return XMA_SUCCESS;
    }
    if (la_ctx->xma_la_frame == NULL) 
	{
        return XMA_ERROR;
    }
    ret = xlnx_la_send_frame(la_ctx, in_frame);
    switch (ret) 
	{
        case XMA_SUCCESS:
            ret = xma_filter_session_recv_frame(la_ctx->filter_session, 
                    la_ctx->xma_la_frame);
            if (ret == XMA_TRY_AGAIN) {
                ret = XMA_SEND_MORE_DATA;
            }
            if(ret != ENC_APP_SUCCESS)
                break;
        case XMA_SEND_MORE_DATA:
            break;
        case XMA_TRY_AGAIN:
            /* If the user is receiving output, this condition should 
               not be hit */
            ret = xma_filter_session_recv_frame(la_ctx->filter_session, la_ctx->xma_la_frame);
            if (ret == XMA_SUCCESS) 
			{
                ret = xlnx_la_send_frame(la_ctx, in_frame);
            }
            break;
        case XMA_ERROR:
        default:
            *out_frame = NULL;
            break;
    }
    if (ret == XMA_SUCCESS) 
	{
        *out_frame = la_ctx->xma_la_frame;
        la_ctx->xma_la_frame = NULL;
    }
    return ret;
}

int32_t xlnx_la_release_frame(XlnxLookaheadCtx *la_ctx, XmaFrame *received_frame)
{
    if (la_ctx->bypass) 
	{
        return ENC_APP_SUCCESS;
    }
    if (!received_frame || la_ctx->xma_la_frame) 
	{
        return ENC_APP_FAILURE;
    }
    la_ctx->xma_la_frame = received_frame;
    XmaSideDataHandle *side_data = la_ctx->xma_la_frame->side_data;
    memset(la_ctx->xma_la_frame, 0, sizeof(XmaFrame));
    la_ctx->xma_la_frame->side_data = side_data;
    return ENC_APP_SUCCESS;
}

int32_t xlnx_la_deinit(XlnxLookaheadCtx *la_ctx, XmaFilterProperties *xma_la_props)
{
    if (!la_ctx) {
        return ENC_APP_FAILURE;
    }
    if (la_ctx->bypass == 0) 
	{
        if (la_ctx->filter_session) {
            xma_filter_session_destroy(la_ctx->filter_session);
            la_ctx->filter_session = NULL;
        }
        if (la_ctx->xma_la_frame != NULL) 
		{
            xma_frame_clear_all_side_data(la_ctx->xma_la_frame);
            free(la_ctx->xma_la_frame);
            la_ctx->xma_la_frame = NULL;
        }
    }
    xlnx_la_free_xma_props(xma_la_props);
    return ENC_APP_SUCCESS;
}

void xlnx_enc_deinit(XlnxEncoderCtx *enc_ctx, XmaEncoderProperties *xma_enc_props)
{
    if(enc_ctx->enc_session != NULL)
        xma_enc_session_destroy(enc_ctx->enc_session);
    free(enc_ctx->enc_props.enc_options);
    xlnx_enc_free_xma_props(xma_enc_props);

    #ifdef U30V2
    if(enc_ctx->xma_buffer.data.buffer) {
        free(enc_ctx->xma_buffer.data.buffer);
    }
    #endif
}

int loadyuv(char *ybuf, char *uvbuf, FILE *hInputYUVFile)
{
	int ylen = 1920 * 1080;
	int uvlen = 1920 * 1080 / 2; 
	
	fread(ybuf, ylen, 1, hInputYUVFile);
	fread(uvbuf, uvlen, 1, hInputYUVFile);

	return 0;
}

XlnxEncoderCtx enc_ctx;
XmaEncoderProperties xma_enc_props;
XmaFilterProperties  xma_la_props;

XlnxDecoderCtx ctx;

int Encoder_Init()
{
	int32_t ret = ENC_APP_SUCCESS;
	
    memset(&enc_ctx, 0, sizeof(enc_ctx));
	
    xlnx_enc_context_init(&enc_ctx);
	
	enc_ctx.enc_props.width=1920;
	enc_ctx.enc_props.height=1080;
	enc_ctx.enc_props.codec_id = 100;

    if(xlnx_enc_update_props(&enc_ctx, &xma_enc_props) != ENC_APP_SUCCESS)
        return ENC_APP_FAILURE;

    xlnx_enc_frame_init(&enc_ctx);

    if((ret = xlnx_enc_device_init(&enc_ctx.enc_xrm_ctx, &xma_enc_props, enc_ctx.enc_props.lookahead_depth)) != ENC_APP_SUCCESS) 
	{
        xma_logmsg(XMA_ERROR_LOG, XLNX_ENC_APP_MODULE, 
                "Device Init failed with error %d \n", ret);
        xlnx_enc_app_close(&enc_ctx, &xma_enc_props, &xma_la_props);
        return -1;
    }

    /* Lookahead session creation*/
    if((ret = xlnx_enc_la_init(&enc_ctx, &xma_la_props)) != ENC_APP_SUCCESS) {
        xlnx_enc_app_close(&enc_ctx, &xma_enc_props, &xma_la_props);
        return -1;
    }

    if((ret = xlnx_enc_create_session(&enc_ctx, &xma_enc_props)) != 
                                                            ENC_APP_SUCCESS) {
        xlnx_enc_app_close(&enc_ctx, &xma_enc_props, &xma_la_props);
        return -1;
    }
	return ENC_APP_SUCCESS;
}

int Encoder_frame(char* iyBuf,char* iuvBuf,char* outBuf,int* outlen)
{
    uint32_t ret = ENC_APP_SUCCESS;
    uint32_t frame_size_y = (enc_ctx.enc_props.width *enc_ctx.enc_props.height);
	uint32_t frame_size_uv = frame_size_y /2 ;
	
	XmaFrame *xma_frame = &(enc_ctx.in_frame);
    xma_frame->data[0].refcount = 1;
    xma_frame->data[0].buffer_type = XMA_HOST_BUFFER_TYPE;
    xma_frame->data[0].is_clone = false;
    xma_frame->data[0].buffer = calloc(1, frame_size_y);
    xma_frame->data[1].refcount = 1;
    xma_frame->data[1].buffer_type = XMA_HOST_BUFFER_TYPE;
    xma_frame->data[1].is_clone = false;
    xma_frame->data[1].buffer = calloc(1, frame_size_uv);

	memcpy((char*)xma_frame->data[0].buffer,iyBuf, frame_size_y);//y
	memcpy((char*)xma_frame->data[1].buffer, iuvBuf,frame_size_uv);//uv
		
	ret = xma_enc_session_send_frame(enc_ctx.enc_session, enc_ctx.enc_in_frame);
	if (ret == XMA_SUCCESS) {
		enc_ctx.enc_state = ENC_GET_OUTPUT;
	}
	else if(ret == XMA_SEND_MORE_DATA) 
	{
		enc_ctx.enc_state = ENC_READ_INPUT;
	}
	else 
	{
		printf("Encoder send frame failed!!\n");
		return ENC_APP_DONE;
	}
	
	int32_t recv_size = 0;
	ret = xma_enc_session_recv_data(enc_ctx.enc_session, &(enc_ctx.xma_buffer), &recv_size);
	if (ret == XMA_SUCCESS) 
	{
		memcpy(outBuf,enc_ctx.xma_buffer.data.buffer,recv_size);
		*outlen = recv_size;
	} 
	else if(ret <= XMA_ERROR) 
	{
		return ENC_APP_DONE;
	}

    return ENC_APP_SUCCESS;
}

void Encoder_Release()
{
	xlnx_enc_app_close(&enc_ctx, &xma_enc_props, &xma_la_props);
}

void Decoder_Init()
{
	memset(&ctx, 0, sizeof(ctx));
    printf("Decoder_Init \n");
	
	xlnx_dec_create_context(&ctx);

    printf("xlnx_dec_create_context \n");
	
    if(xlnx_dec_fpga_init(&ctx) != DEC_APP_SUCCESS)
	{
        printf("xlnx_dec_fpga_init \n");
        xlnx_dec_cleanup_ctx(&ctx);
        exit(DEC_APP_ERROR);
    }

    ctx.xma_dec_session = xma_dec_session_create(&ctx.dec_xma_props);
    if(!ctx.xma_dec_session)
	{
        DECODER_APP_LOG_ERROR("Failed to create decoder session\n");
        xlnx_dec_cleanup_ctx(&ctx);
        exit(DEC_APP_ERROR);
    }
}

void Decoder_release()
{
    xlnx_dec_cleanup_ctx(&ctx);
    printf("Decoder_release\n");
}

int xvbm_conv_get_plane_size(int32_t width,int32_t height,XmaFormatType format,int32_t plane_id)
{
    int p_size;

    switch (format) {
        case XMA_YUV420_FMT_TYPE:
            switch(plane_id) {
                case 0:  p_size = width * height;        break;
                case 1:  p_size = ((width * height)>>2); break;
                case 2:  p_size = ((width * height)>>2); break;
                default: p_size = 0;                     break;
            }
            break;

        case XMA_YUV422_FMT_TYPE:
            switch(plane_id) {
                case 0:  p_size = width * height;        break;
                case 1:  p_size = ((width * height)>>1); break;
                case 2:  p_size = ((width * height)>>1); break;
                default: p_size = 0;                     break;
            }
            break;

        case XMA_YUV444_FMT_TYPE:
        case XMA_RGBP_FMT_TYPE:
            p_size = (width * height);
            break;

        default:
              printf("xvbm_conv:: Unsupported format...\n");
              p_size = 0;
              break;
    }
    return(p_size);
}

int dec_output_data(unsigned char* hostbuf,unsigned char* outbuf)
{
	XlnxDecoderCtx* fctx =&ctx;
    int    stride          = fctx->dec_params.width;
    int    aligned_width   = ALIGN(fctx->dec_params.width, STRIDE_ALIGN);
    int    aligned_height  = ALIGN(fctx->dec_params.height, HEIGHT_ALIGN);
    size_t plane_size      = fctx->dec_params.width * fctx->dec_params.height;
    size_t buff_plane_size = aligned_width * aligned_height;
    int offset = 0;
	int offsetbuf = 0;
    int bytes_written;
    int bytes_read;
    int num_planes = xma_frame_planes_get(&fctx->channel_ctx.xframe->frame_props);
	
	//printf("==== %d %d %d %d %d \n",stride,aligned_width,aligned_height,plane_size,buff_plane_size);//1920 2048 1088 2073600 2228224
    for(int plane_id = 0; plane_id < num_planes; plane_id++) 
	{
        bytes_written = 0;
        bytes_read = 0;
        if(plane_id > 0) 
		{
            buff_plane_size = aligned_width * aligned_width / 2;
            plane_size = fctx->dec_params.width * fctx->dec_params.height / 2;
        }
        while(bytes_read < buff_plane_size) 
		{
            if(bytes_written < plane_size) 
			{
				memcpy(outbuf+ offsetbuf,hostbuf + offset,stride);
                bytes_written += stride;
				offsetbuf += stride;
            }
            offset += aligned_width;
            bytes_read += aligned_width;
        }
    }
    return DEC_APP_SUCCESS;
}

int Decoder_frame(unsigned char* inbuffer,unsigned char* outbuffer,int insize)
{
    int ret;
	int data_used = 0;
	int rc        = XMA_ERROR;
	XlnxDecoderCtx* dec_ctx = &ctx;
    int pts       = dec_ctx->pts;
    XmaDataBuffer* xbuffer = (XmaDataBuffer*)malloc(3840*2160*3);
	xbuffer->data.buffer = inbuffer;
	xbuffer->alloc_size  = insize;
	xbuffer->is_eof 	 = 0;
	xbuffer->pts		 = pts;

    while(1){
        rc = xma_dec_session_send_data(dec_ctx->xma_dec_session, xbuffer, &data_used);
        if(rc == XMA_ERROR) 
        {
            DECODER_APP_LOG_ERROR("Error sending data to decoder. Data %d\n", dec_ctx->num_frames_sent);
            exit(DEC_APP_ERROR);
        }

        if(rc == XMA_TRY_AGAIN) 
        {
            printf("==========XMA_TRY_AGAIN============\n");
        }

        if(rc == XMA_SUCCESS){}
    }
	
	
    ret = xma_dec_session_recv_frame(dec_ctx->xma_dec_session, dec_ctx->channel_ctx.xframe);
    if(ret == XMA_SUCCESS)
	{
		size_t buffer_size =0;
		// int size = 1920*1080;
		unsigned char* hbuf = xlnx_dec_get_buffer_from_fpga(dec_ctx, &buffer_size);
		dec_output_data(hbuf,outbuffer);
    }
	else 
	{
        usleep(5);
    }

    return ret;
}

int dec_write_host_buffer_to_file(unsigned char* hostbuf,FILE* file)
{
	XlnxDecoderCtx* fctx =&ctx;
    int    stride          = fctx->dec_params.width;
    int    aligned_width   = ALIGN(fctx->dec_params.width, STRIDE_ALIGN);
    int    aligned_height  = ALIGN(fctx->dec_params.height, HEIGHT_ALIGN);
    size_t plane_size      = fctx->dec_params.width * fctx->dec_params.height;
    size_t buff_plane_size = aligned_width * aligned_height;
    int offset = 0;
    int bytes_written;
    int bytes_read;
    int num_planes = xma_frame_planes_get(&fctx->channel_ctx.xframe->frame_props);
    for(int plane_id = 0; plane_id < num_planes; plane_id++) 
	{
        bytes_written = 0;
        bytes_read = 0;
        if(plane_id > 0) 
		{
            buff_plane_size = aligned_width * aligned_width / 2;
            plane_size = fctx->dec_params.width * fctx->dec_params.height / 2;
        }
        while(bytes_read < buff_plane_size) 
		{
            if(bytes_written < plane_size) 
			{
                fwrite(hostbuf + offset, 1, stride, file);
                bytes_written += stride;
            }
            offset += aligned_width;
            bytes_read += aligned_width;
        }
    }
    fflush(file);
    return DEC_APP_SUCCESS;
}

char* filebuf_;
const char* pbuf_;
int filesize_;
unsigned char is_stop_;
const char* AVCFindStartCodeInternal(const char *p, const char *end)
{
	const char *a = p + 4 - ((ptrdiff_t)p & 3);
	for (end -= 3; p < a && p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}
	for (end -= 3; p < end; p += 4) {
		unsigned int x = *(const unsigned int*)p;
		//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
		//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
		if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
			if (p[1] == 0) {
				if (p[0] == 0 && p[2] == 1)
					return p;
				if (p[2] == 0 && p[3] == 1)
					return p + 1;
			}
			if (p[3] == 0) {
				if (p[2] == 0 && p[4] == 1)
					return p + 2;
				if (p[4] == 0 && p[5] == 1)
					return p + 3;
			}
		}
	}
	for (end += 3; p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}
	return end + 3;
}

const char* AVCFindStartCode(const char *p, const char *end)
{
	const char *out = AVCFindStartCodeInternal(p, end);
	if (p<out && out<end && !out[-1]) out--;
	return out;
}

int H264FrameReader_Init(const char* filename)
{
	FILE* fp = fopen(filename, "rb");
	filebuf_ = 0;
	filesize_ = 0;
	int retval =0;
	if (fp)
	{
		fseek(fp, 0, SEEK_END);
		filesize_ = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		filebuf_ = (char*)malloc(filesize_);
		retval = fread(filebuf_, 1, filesize_, fp);
		fclose(fp);
	}
	pbuf_ = filebuf_;
	
	return retval;
}

void H264FrameReader_Free()
{
	free(filebuf_);
}

int H264FrameReader_ReadFrame(unsigned char* outBuf, int* outBufSize)
{
	unsigned char* pbufout = 0;
	const unsigned char *p = 0;
	const unsigned char *end = 0;
	const unsigned char *nal_start, *nal_end;
	unsigned char startcodebuf[] = { 0x00, 0x00, 0x00, 0x01 };
	if (pbuf_ >= filebuf_ + filesize_)
	{
		return 0;
	}

	pbufout = outBuf;
	p = pbuf_;
	end = filebuf_ + filesize_;

	nal_start = AVCFindStartCode(p, end);
	while (nal_start < end)
	{
		unsigned int nal_size = 0;
		unsigned char nal_type = 0;

		while (!*(nal_start++));

		nal_end = AVCFindStartCode(nal_start, end);

		nal_size = nal_end - nal_start;
		nal_type = nal_start[0] & 0x1f;

		memcpy(pbufout, startcodebuf, 4);
		pbufout += 4;
		memcpy(pbufout, nal_start, nal_size);
		pbufout += nal_size;

		nal_start = nal_end;
		break;
	}

	*outBufSize = pbufout - outBuf;
	pbuf_ = nal_start;

	return 1;
}