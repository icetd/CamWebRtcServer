#include <bits/stdint-uintn.h>
#include <x264encoder.h>
#include <log.h>
#include <cstring>
#include <stdlib.h>

X264Encoder::X264Encoder(X264_Param_t param) :
    m_x264_param(param),
    m_threadId(0)
{
    initialize();
}

X264Encoder::~X264Encoder()
{
    free(param);
    free(pic_in);
    free(pic_out);
}


int X264Encoder::initialize()
{
    isStop = false;

    param = (x264_param_t *)malloc(sizeof(x264_param_t));
    pic_in = (x264_picture_t *)malloc(sizeof(x264_picture_t));
    pic_out = (x264_picture_t *)malloc(sizeof(x264_picture_t));

    pic_in->i_pts = 0;
    
    // 使用更激进的零延迟预设
    x264_param_default_preset(param, "ultrafast", "zerolatency");
    
    param->i_csp = m_x264_param.colorSpace;
    param->i_threads = 1;  // WebRTC 建议单线程
    param->i_width = m_x264_param.width;
    param->i_height = m_x264_param.height;
    param->i_fps_den = 1;
    param->i_fps_num = m_x264_param.fps;

    if (m_x264_param.method == "CRF") {
        param->rc.f_rf_constant = m_x264_param.rf_constant;
        param->rc.i_rc_method = X264_RC_CRF;
    } else if (m_x264_param.method == "ABR") {
        param->rc.i_bitrate = m_x264_param.bitrate;
        param->rc.i_rc_method = X264_RC_ABR;
    }

    // ========== WebRTC 关键配置（修正版）==========
    
    // 1. 零延迟配置
    param->i_sync_lookahead = 0;
    param->i_bframe = 0;                    // 禁用 B 帧
    param->b_sliced_threads = 0;            // 禁用切片线程
    param->b_vfr_input = 0;
    param->b_intra_refresh = 0;              // 禁用帧内刷新（WebRTC 不需要）
    
    // 2. 关键帧间隔（30fps 下每 2 秒一个关键帧）
    param->i_keyint_min = 30;                // 最少间隔
    param->i_keyint_max = 60;                // 最多间隔
    
    // 3. WebRTC 兼容配置
    param->b_cabac = 0;                     // Baseline 必须用 CAVAC
    param->i_log_level = X264_LOG_INFO;
    
    // 4. 低延迟运动估计
    param->analyse.i_me_method = X264_ME_DIA;     // 最低延迟
    param->analyse.i_subpel_refine = 0;           // 最低精度（0-2）
    param->analyse.i_trellis = 0;                 // 禁用 trellis
    param->analyse.b_transform_8x8 = 0;           // 禁用 8x8 变换
    
    // 5. 码率控制优化
    param->rc.i_aq_mode = 0;                // 临时禁用自适应量化
    param->rc.i_lookahead = 0;               // 零前瞻
    
    // 6. VBV 放宽限制（关键修改！）
    if (m_x264_param.method == "ABR") {
        int target_bitrate = m_x264_param.bitrate;
        param->rc.i_vbv_max_bitrate = target_bitrate * 2;     // 放宽到 2 倍
        param->rc.i_vbv_buffer_size = target_bitrate;          // 1 秒缓冲
    } else {
        param->rc.i_vbv_max_bitrate = 0;    // CRF 模式不限制
        param->rc.i_vbv_buffer_size = 0;
    }
    
    // 7. 启用场景切换检测（适度）
    param->i_scenecut_threshold = 0;        // 完全禁用，避免意外关键帧
    
    // 8. Annex B 格式（重要！）
    param->b_annexb = 1;
    param->b_repeat_headers = 1;             // 每个关键帧重复 SPS/PPS
    
    // 9. WebRTC 标准 Profile
    x264_param_apply_profile(param, "baseline");

    pHandle = x264_encoder_open(param);
    
    if (!pHandle) {
        LOG(ERROR, "open x264 encoder failed.");
        return -1;
    }

    x264_picture_init(pic_out);
    x264_picture_alloc(pic_in, param->i_csp, param->i_width, param->i_height);
    
    pic_in->img.i_csp = param->i_csp;
    pic_in->img.i_plane = 3;
    
    // 打印配置信息用于调试
    LOG(INFO, "x264 encoder initialized: %dx%d @ %dfps, bitrate=%d, profile=baseline",
        param->i_width, param->i_height, param->i_fps_num, param->rc.i_bitrate);

    return 0;
}

/** 
/**  @brief encode YUV422 to H.264
 *   @param 	inbuf  YUV422 frame
 *   @param 	insize YUV422 frame size
 *   @param 	outbuf H.264 frame
 *   @param     format YUV422 fotmat |YUY2 camera| or |YUV422 from DeCompress|
 *   @return    the size of H.264 frame
 */
int X264Encoder::encode(uint8_t *inbuf, int insize, uint8_t *outbuf, std::string &format)
{
    int size = 0;
    uint8_t *out = outbuf;
    
    int width = m_x264_param.width;
    int height = m_x264_param.height;
    
    if (format == "MJPEG") {
        // MJPEG 解码后应该是 YUV420P 或者 YUV422P
        int y_size = width * height;
        int uv_size = y_size / 4;
        uint8_t *y = pic_in->img.plane[0];
        uint8_t *u = pic_in->img.plane[1];
        uint8_t *v = pic_in->img.plane[2];

        if (insize == y_size * 3 / 2) {
            // YUV420P
            memcpy(y, inbuf, y_size);
            memcpy(u, inbuf + y_size, y_size / 4);
            memcpy(v, inbuf + y_size + y_size / 4, y_size / 4);
        } else if (insize == y_size * 2) {
            // YUV422P
            memcpy(y, inbuf, y_size);

            uint8_t *src_u = inbuf + y_size;
            uint8_t *src_v = src_u + y_size / 2;

            // 422 -> 420
            for (int row = 0; row < height / 2; row++) {
                memcpy(
                    u + row * width / 2,
                    src_u + (row * 2) * width / 2,
                    width / 2);

                memcpy(
                    v + row * width / 2,
                    src_v + (row * 2) * width / 2,
                    width / 2);
            }
        }
    } 
    else if (format == "YUY2") {
        // YUY2 格式：Y0 U0 Y1 V0 Y2 U1 Y3 V1 ...
        // 需要转换为 YUV420P（Y 平面大小 w*h，U/V 平面大小 w/2 * h/2）
        
        uint8_t *y = pic_in->img.plane[0];
        uint8_t *u = pic_in->img.plane[1];
        uint8_t *v = pic_in->img.plane[2];
        
        int y_index = 0;
        int u_index = 0;
        int v_index = 0;

        for (int h = 0; h < height; h += 2) {
            for (int w = 0; w < width; w += 2) {
                int p0 = (h * width + w) * 2;
                int p1 = (h * width + w + 1) * 2;
                int p2 = ((h + 1) * width + w) * 2;
                int p3 = ((h + 1) * width + w + 1) * 2;

                uint8_t Y0 = inbuf[p0];
                uint8_t U = inbuf[p0 + 1];
                uint8_t Y1 = inbuf[p1];
                uint8_t V = inbuf[p1 + 1];

                y[(h * width) + w] = Y0;
                y[(h * width) + w + 1] = Y1;
                y[((h + 1) * width) + w] = inbuf[p2];
                y[((h + 1) * width) + w + 1] = inbuf[p3];

                u[(h / 2) * (width / 2) + (w / 2)] = U;
                v[(h / 2) * (width / 2) + (w / 2)] = V;
            }
        }
    }

    pic_in->i_pts = pts;
    pts++;

    int ret = x264_encoder_encode(pHandle, &nals, &num_nals, pic_in, pic_out);
    if (ret < 0) {
        LOG(ERROR, "x264 encode failed.");
        return -1;
    }

    for (int j = 0; j < num_nals; j++) {
        memcpy(out, nals[j].p_payload, nals[j].i_payload);
        size += nals[j].i_payload;
        out += nals[j].i_payload;
    }
    
    if (size > 0 && (pts % 30 == 0)) {
        LOG(INFO, "h264 pts:%d frame size: %d", pic_in->i_pts, size);
    }

    return size;
}

int X264Encoder::startCode3(char *buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
        return 1;
    else
        return 0;
}

int X264Encoder::startCode4(char *buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1)
        return 1;
    else
        return 0;
}