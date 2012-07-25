/*
 *      Copyright (C) 2010 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#elif defined(_WIN32)
#include "system.h"
#endif

#include "OMXVideo.h"

#include "OMXStreamInfo.h"
#include "utils/log.h"
#include "linux/XMemUtils.h"

#include <sys/time.h>
#include <inttypes.h>

#ifdef CLASSNAME
#undef CLASSNAME
#endif
#define CLASSNAME "COMXVideo"

#if 0
// TODO: These are Nvidia Tegra2 dependent, need to dynamiclly find the
// right codec matched to video format.
#define OMX_H264BASE_DECODER    "OMX.Nvidia.h264.decode"
// OMX.Nvidia.h264ext.decode segfaults, not sure why.
//#define OMX_H264MAIN_DECODER  "OMX.Nvidia.h264ext.decode"
#define OMX_H264MAIN_DECODER    "OMX.Nvidia.h264.decode"
#define OMX_H264HIGH_DECODER    "OMX.Nvidia.h264ext.decode"
#define OMX_MPEG4_DECODER       "OMX.Nvidia.mp4.decode"
#define OMX_MPEG4EXT_DECODER    "OMX.Nvidia.mp4ext.decode"
#define OMX_MPEG2V_DECODER      "OMX.Nvidia.mpeg2v.decode"
#define OMX_VC1_DECODER         "OMX.Nvidia.vc1.decode"
#endif

#define OMX_VIDEO_DECODER       "OMX.broadcom.video_decode"
#define OMX_H264BASE_DECODER    OMX_VIDEO_DECODER
#define OMX_H264MAIN_DECODER    OMX_VIDEO_DECODER
#define OMX_H264HIGH_DECODER    OMX_VIDEO_DECODER
#define OMX_MPEG4_DECODER       OMX_VIDEO_DECODER
#define OMX_MSMPEG4V1_DECODER   OMX_VIDEO_DECODER
#define OMX_MSMPEG4V2_DECODER   OMX_VIDEO_DECODER
#define OMX_MSMPEG4V3_DECODER   OMX_VIDEO_DECODER
#define OMX_MPEG4EXT_DECODER    OMX_VIDEO_DECODER
#define OMX_MPEG2V_DECODER      OMX_VIDEO_DECODER
#define OMX_VC1_DECODER         OMX_VIDEO_DECODER
#define OMX_WMV3_DECODER        OMX_VIDEO_DECODER
#define OMX_VP8_DECODER         OMX_VIDEO_DECODER

#define MAX_TEXT_LENGTH 1024

COMXVideo::COMXVideo()
{
  m_is_open           = false;
  m_Pause             = false;
  m_setStartTime      = true;
  m_setStartTimeText  = true;
  m_extradata         = NULL;
  m_extrasize         = 0;
  m_converter         = NULL;
  m_video_convert     = false;
  m_video_codec_name  = "";
  m_deinterlace       = false;
  m_hdmi_clock_sync   = false;
  m_first_frame       = true;
  m_first_text        = true;
}

COMXVideo::~COMXVideo()
{
  if (m_is_open)
    Close();
}

bool COMXVideo::SendDecoderConfig()
{
  OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

  /* send decoder config */
  if(m_extrasize > 0 && m_extradata != NULL)
  {
    OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer();

    if(omx_buffer == NULL)
    {
      CLog::Log(LOGERROR, "%s::%s - buffer error 0x%08x", CLASSNAME, __func__, omx_err);
      return false;
    }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = m_extrasize;
    if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
    {
      CLog::Log(LOGERROR, "%s::%s - omx_buffer->nFilledLen > omx_buffer->nAllocLen", CLASSNAME, __func__);
      return false;
    }

    memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy((unsigned char *)omx_buffer->pBuffer, m_extradata, omx_buffer->nFilledLen);
    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
  
    omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
      return false;
    }
  }
  return true;
}

bool COMXVideo::Open(COMXStreamInfo &hints, OMXClock *clock, bool deinterlace, bool hdmi_clock_sync)
{
  OMX_ERRORTYPE omx_err   = OMX_ErrorNone;
  std::string decoder_name;

  m_video_codec_name      = "";
  m_codingType            = OMX_VIDEO_CodingUnused;

  m_decoded_width  = hints.width;
  m_decoded_height = hints.height;

  m_hdmi_clock_sync = hdmi_clock_sync;

  if(!m_decoded_width || !m_decoded_height)
    return false;

  m_converter     = new CBitstreamConverter();
  m_video_convert = m_converter->Open(hints.codec, (uint8_t *)hints.extradata, hints.extrasize, false);

  if(m_video_convert)
  {
    if(m_converter->GetExtraData() != NULL && m_converter->GetExtraSize() > 0)
    {
      m_extrasize = m_converter->GetExtraSize();
      m_extradata = (uint8_t *)malloc(m_extrasize);
      memcpy(m_extradata, m_converter->GetExtraData(), m_converter->GetExtraSize());
    }
  }
  else
  {
    if(hints.extrasize > 0 && hints.extradata != NULL)
    {
      m_extrasize = hints.extrasize;
      m_extradata = (uint8_t *)malloc(m_extrasize);
      memcpy(m_extradata, hints.extradata, hints.extrasize);
    }
  }

  switch (hints.codec)
  {
    case CODEC_ID_H264:
    {
      switch(hints.profile)
      {
        case FF_PROFILE_H264_BASELINE:
          // (role name) video_decoder.avc
          // H.264 Baseline profile
          decoder_name = OMX_H264BASE_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        case FF_PROFILE_H264_MAIN:
          // (role name) video_decoder.avc
          // H.264 Main profile
          decoder_name = OMX_H264MAIN_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        case FF_PROFILE_H264_HIGH:
          // (role name) video_decoder.avc
          // H.264 Main profile
          decoder_name = OMX_H264HIGH_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        case FF_PROFILE_UNKNOWN:
          decoder_name = OMX_H264HIGH_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
        default:
          decoder_name = OMX_H264HIGH_DECODER;
          m_codingType = OMX_VIDEO_CodingAVC;
          m_video_codec_name = "omx-h264";
          break;
      }
    }
    break;
    case CODEC_ID_MPEG4:
      // (role name) video_decoder.mpeg4
      // MPEG-4, DivX 4/5 and Xvid compatible
      decoder_name = OMX_MPEG4_DECODER;
      m_codingType = OMX_VIDEO_CodingMPEG4;
      m_video_codec_name = "omx-mpeg4";
      break;
    case CODEC_ID_MPEG1VIDEO:
    case CODEC_ID_MPEG2VIDEO:
      // (role name) video_decoder.mpeg2
      // MPEG-2
      decoder_name = OMX_MPEG2V_DECODER;
      m_codingType = OMX_VIDEO_CodingMPEG2;
      m_video_codec_name = "omx-mpeg2";
      break;
    case CODEC_ID_H263:
      // (role name) video_decoder.mpeg4
      // MPEG-4, DivX 4/5 and Xvid compatible
      decoder_name = OMX_MPEG4_DECODER;
      m_codingType = OMX_VIDEO_CodingMPEG4;
      m_video_codec_name = "omx-h263";
      break;
    case CODEC_ID_VP8:
      // (role name) video_decoder.vp8
      // VP8
      decoder_name = OMX_VP8_DECODER;
      m_codingType = OMX_VIDEO_CodingVP8;
      m_video_codec_name = "omx-vp8";
    break;
    case CODEC_ID_VC1:
      // (role name) video_decoder.vc1
      // VC-1, WMV9
      decoder_name = OMX_VC1_DECODER;
      m_codingType = OMX_VIDEO_CodingWMV;
      m_video_codec_name = "omx-vc1";
      break;
    /*
    case CODEC_ID_WMV3:
      // (role name) video_decoder.wmv3
      //WMV3
      decoder_name = OMX_WMV3_DECODER;
      m_codingType = OMX_VIDEO_CodingWMV;
      m_video_codec_name = "omx-wmv3";
      break;
    */
    default:
      return false;
    break;
  }

  if(m_decoded_width <= 720 && m_decoded_height <=576 && deinterlace)
  {
    printf("enable deinterlace\n");
    m_deinterlace = true;
  }
  else
  {
    m_deinterlace = false;
  }

  std::string componentName = "";

  componentName = decoder_name;
  if(!m_omx_decoder.Initialize(componentName, OMX_IndexParamVideoInit))
    return false;

  componentName = "OMX.broadcom.video_render";
  if(!m_omx_render.Initialize(componentName, OMX_IndexParamVideoInit))
    return false;

  componentName = "OMX.broadcom.video_scheduler";
  if(!m_omx_sched.Initialize(componentName, OMX_IndexParamVideoInit))
    return false;

  if(m_deinterlace)
  {
    componentName = "OMX.broadcom.image_fx";
    if(!m_omx_image_fx.Initialize(componentName, OMX_IndexParamImageInit))
      return false;
  }

  componentName = "OMX.broadcom.text_scheduler";
  if(!m_omx_text.Initialize(componentName, OMX_IndexParamOtherInit))
    return false;

  if(clock == NULL)
    return false;

  m_av_clock = clock;
  m_omx_clock = m_av_clock->GetOMXClock();

  if(m_omx_clock->GetComponent() == NULL)
  {
    m_av_clock = NULL;
    m_omx_clock = NULL;
    return false;
  }

  if(m_deinterlace)
  {
    m_omx_tunnel_decoder.Initialize(&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_image_fx, m_omx_image_fx.GetInputPort());
    m_omx_tunnel_image_fx.Initialize(&m_omx_image_fx, m_omx_image_fx.GetOutputPort(), &m_omx_sched, m_omx_sched.GetInputPort());
  }
  else
  {
    m_omx_tunnel_decoder.Initialize(&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_sched, m_omx_sched.GetInputPort());
  }
  m_omx_tunnel_sched.Initialize(&m_omx_sched, m_omx_sched.GetOutputPort(), &m_omx_render, m_omx_render.GetInputPort());
  m_omx_tunnel_clock.Initialize(m_omx_clock, m_omx_clock->GetInputPort() + 1, &m_omx_sched, m_omx_sched.GetOutputPort() + 1);
  m_omx_tunnel_text.Initialize(m_omx_clock, m_omx_clock->GetInputPort() + 2, &m_omx_text, m_omx_text.GetInputPort() + 2);

  omx_err = m_omx_tunnel_clock.Establish(false);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open m_omx_tunnel_clock.Establish\n");
    return false;
  }

  omx_err = m_omx_decoder.SetStateForComponent(OMX_StateIdle);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open m_omx_decoder.SetStateForComponent\n");
    return false;
  }

  OMX_VIDEO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = m_omx_decoder.GetInputPort();
  formatType.eCompressionFormat = m_codingType;

  if (hints.fpsscale > 0 && hints.fpsrate > 0)
  {
    formatType.xFramerate = (long long)(1<<16)*hints.fpsrate / hints.fpsscale;
  }
  else
  {
    formatType.xFramerate = 25 * (1<<16);
  }

  omx_err = m_omx_decoder.SetParameter(OMX_IndexParamVideoPortFormat, &formatType);
  if(omx_err != OMX_ErrorNone)
    return false;
  
  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = m_omx_decoder.GetInputPort();

  omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &portParam);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
    return false;
  }

  portParam.nPortIndex = m_omx_decoder.GetInputPort();
  portParam.nBufferCountActual = VIDEO_BUFFERS;

  omx_err = m_omx_decoder.SetParameter(OMX_IndexParamPortDefinition, &portParam);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
    return false;
  }

  OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE concanParam;
  OMX_INIT_STRUCTURE(concanParam);
  concanParam.bStartWithValidFrame = OMX_FALSE;

  omx_err = m_omx_decoder.SetParameter(OMX_IndexParamBrcmVideoDecodeErrorConcealment, &concanParam);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error OMX_IndexParamBrcmVideoDecodeErrorConcealment omx_err(0x%08x)\n", omx_err);
    return false;
  }

  if(m_hdmi_clock_sync)
  {
    OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
    OMX_INIT_STRUCTURE(latencyTarget);
    latencyTarget.nPortIndex = m_omx_render.GetInputPort();
    latencyTarget.bEnabled = OMX_TRUE;
    latencyTarget.nFilter = 2;
    latencyTarget.nTarget = 4000;
    latencyTarget.nShift = 3;
    latencyTarget.nSpeedFactor = -135;
    latencyTarget.nInterFactor = 500;
    latencyTarget.nAdjCap = 20;

    omx_err = m_omx_render.SetConfig(OMX_IndexConfigLatencyTarget, &latencyTarget);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXVideo::Open OMX_IndexConfigLatencyTarget error (0%08x)\n", omx_err);
      return false;
    }
  }

  // Alloc buffers for the omx intput port.
  omx_err = m_omx_decoder.AllocInputBuffers();
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open AllocOMXInputBuffers error (0%08x)\n", omx_err);
    return false;
  }

  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = m_omx_text.GetInputPort();

  omx_err = m_omx_text.GetParameter(OMX_IndexParamPortDefinition, &portParam);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
    return false;
  }

  portParam.nBufferCountActual  = 100;
  portParam.nBufferSize         = MAX_TEXT_LENGTH;

  omx_err = m_omx_text.SetParameter(OMX_IndexParamPortDefinition, &portParam);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
    return false;
  }

  omx_err = m_omx_text.AllocInputBuffers();
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open AllocOMXInputBuffers\n");
    return false;
  }

  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = m_omx_text.GetOutputPort();

  omx_err = m_omx_text.GetParameter(OMX_IndexParamPortDefinition, &portParam);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
    return false;
  }

  portParam.eDir = OMX_DirOutput;
  portParam.format.other.eFormat = OMX_OTHER_FormatText;
  portParam.format.other.eFormat = OMX_OTHER_FormatText;
  portParam.nBufferCountActual  = 1;
  portParam.nBufferSize         = MAX_TEXT_LENGTH;

  omx_err = m_omx_text.SetParameter(OMX_IndexParamPortDefinition, &portParam);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
    return false;
  }

  omx_err = m_omx_text.AllocOutputBuffers();
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open AllocOutputBuffers\n");
    return false;
  }

  omx_err = m_omx_tunnel_decoder.Establish(false);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open m_omx_tunnel_decoder.Establish\n");
    return false;
  }

  omx_err = m_omx_decoder.SetStateForComponent(OMX_StateExecuting);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error m_omx_decoder.SetStateForComponent\n");
    return false;
  }

  if(m_deinterlace)
  {
    OMX_CONFIG_IMAGEFILTERPARAMSTYPE image_filter;
    OMX_INIT_STRUCTURE(image_filter);

    image_filter.nPortIndex = m_omx_image_fx.GetOutputPort();
    image_filter.nNumParams = 1;
    image_filter.nParams[0] = 3;
    image_filter.eImageFilter = OMX_ImageFilterDeInterlaceAdvanced;

    omx_err = m_omx_image_fx.SetConfig(OMX_IndexConfigCommonImageFilterParameters, &image_filter);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXVideo::Open error OMX_IndexConfigCommonImageFilterParameters omx_err(0x%08x)\n", omx_err);
      return false;
    }

    omx_err = m_omx_tunnel_image_fx.Establish(false);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXVideo::Open m_omx_tunnel_image_fx.Establish\n");
      return false;
    }

    omx_err = m_omx_image_fx.SetStateForComponent(OMX_StateExecuting);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXVideo::Open error m_omx_image_fx.SetStateForComponent\n");
      return false;
    }

    m_omx_image_fx.DisablePort(m_omx_image_fx.GetInputPort(), false);
    m_omx_image_fx.DisablePort(m_omx_image_fx.GetOutputPort(), false);
  }

  omx_err = m_omx_tunnel_sched.Establish(false);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open m_omx_tunnel_sched.Establish\n");
    return false;
  }

  omx_err = m_omx_text.SetStateForComponent(OMX_StateExecuting);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error m_omx_text.SetStateForComponent\n");
    return false;
  }

  omx_err = m_omx_tunnel_text.Establish(false);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open m_omx_tunnel_text.Establish\n");
    return false;
  }

  OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_text.GetOutputBuffer(10);
  if(!omx_buffer)
    return false;
  omx_err = m_omx_text.FillThisBuffer(omx_buffer);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open FillThisBuffer\n");
    return false;
  }
  omx_buffer = NULL;

  omx_err = m_omx_sched.SetStateForComponent(OMX_StateExecuting);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error m_omx_sched.SetStateForComponent\n");
    return false;
  }

  omx_err = m_omx_render.SetStateForComponent(OMX_StateExecuting);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXVideo::Open error m_omx_render.SetStateForComponent\n");
    return false;
  }

  if(!SendDecoderConfig())
    return false;

  m_is_open           = true;
  m_drop_state        = false;
  m_setStartTime      = true;
  m_setStartTimeText  = true;
  
  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = m_omx_render.GetInputPort();

  configDisplay.set      = OMX_DISPLAY_SET_PIXEL;
  configDisplay.pixel_x  = hints.pixel_x;
  configDisplay.pixel_y  = hints.pixel_y;

  omx_err = m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);
  if(omx_err != OMX_ErrorNone)
    return false;

  /*

  configDisplay.set     = OMX_DISPLAY_SET_LAYER;
  configDisplay.layer   = 2;

  omx_err = m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);
  if(omx_err != OMX_ErrorNone)
    return false;

  configDisplay.set     = OMX_DISPLAY_SET_DEST_RECT;
  configDisplay.dest_rect.x_offset  = 100;
  configDisplay.dest_rect.y_offset  = 100;
  configDisplay.dest_rect.width     = 640;
  configDisplay.dest_rect.height    = 480;
    
  omx_err = m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);
  if(omx_err != OMX_ErrorNone)
    return false;

  configDisplay.set     = OMX_DISPLAY_SET_TRANSFORM;
  configDisplay.transform = OMX_DISPLAY_ROT180;
    
  omx_err = m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);
  if(omx_err != OMX_ErrorNone)
    return false;

  configDisplay.set     = OMX_DISPLAY_SET_FULLSCREEN;
  configDisplay.fullscreen = OMX_FALSE;
    
  omx_err = m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);
  if(omx_err != OMX_ErrorNone)
    return false;

  configDisplay.set     = OMX_DISPLAY_SET_MODE;
  configDisplay.mode    = OMX_DISPLAY_MODE_FILL; //OMX_DISPLAY_MODE_LETTERBOX;
    
  omx_err = m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);
  if(omx_err != OMX_ErrorNone)
    return false;

  configDisplay.set     = OMX_DISPLAY_SET_LAYER;
  configDisplay.layer   = 1;

  omx_err = m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);
  if(omx_err != OMX_ErrorNone)
    return false;

  configDisplay.set     = OMX_DISPLAY_SET_ALPHA;
  configDisplay.alpha   = OMX_FALSE;
    
  omx_err = m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);
  if(omx_err != OMX_ErrorNone)
    return false;

  */

  CLog::Log(LOGDEBUG,
    "%s::%s - decoder_component(0x%p), input_port(0x%x), output_port(0x%x) deinterlace %d hdmiclocksync %d\n",
    CLASSNAME, __func__, m_omx_decoder.GetComponent(), m_omx_decoder.GetInputPort(), m_omx_decoder.GetOutputPort(),
    m_deinterlace, m_hdmi_clock_sync);

  m_first_frame   = true;
  m_first_text    = true;
  return true;
}

void COMXVideo::Close()
{
  m_omx_tunnel_decoder.Flush();
  if(m_deinterlace)
    m_omx_tunnel_image_fx.Flush();
  m_omx_tunnel_clock.Flush();
  m_omx_tunnel_sched.Flush();
  m_omx_tunnel_text.Flush();

  m_omx_tunnel_clock.Deestablish();
  m_omx_tunnel_decoder.Deestablish();
  if(m_deinterlace)
    m_omx_tunnel_image_fx.Deestablish();
  m_omx_tunnel_sched.Deestablish();
  m_omx_tunnel_text.Deestablish();

  m_omx_decoder.FlushInput();

  m_omx_sched.Deinitialize();
  if(m_deinterlace)
    m_omx_image_fx.Deinitialize();
  m_omx_decoder.Deinitialize();
  m_omx_render.Deinitialize();
  m_omx_text.Deinitialize();

  m_is_open       = false;

  if(m_extradata)
    free(m_extradata);
  m_extradata = NULL;
  m_extrasize = 0;

  if(m_converter)
    delete m_converter;
  m_converter         = NULL;
  m_video_convert     = false;
  m_video_codec_name  = "";
  m_deinterlace       = false;
  m_first_frame       = true;
  m_first_text        = true;
  m_setStartTime      = true;
  m_setStartTimeText  = true;
}

void COMXVideo::SetDropState(bool bDrop)
{
  m_drop_state = bDrop;
}

unsigned int COMXVideo::GetFreeSpace()
{
  return m_omx_decoder.GetInputBufferSpace();
}

unsigned int COMXVideo::GetSize()
{
  return m_omx_decoder.GetInputBufferSize();
}

OMXPacket *COMXVideo::GetText()
{
  OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_text.GetOutputBuffer(10);
  OMXPacket *pkt = NULL;

  if(omx_buffer)
  {
    if(omx_buffer->nFilledLen)
    {
      float pts = FromOMXTime(omx_buffer->nTimeStamp);

      pkt = OMXReader::AllocPacket(omx_buffer->nFilledLen + 1);

      if(pkt)
      {
        pkt->size = omx_buffer->nFilledLen + 1;
        memcpy(pkt->data, omx_buffer->pBuffer, omx_buffer->nFilledLen);
        pkt->pts = pts;
        pkt->dts = pts;
      }
    }

    m_omx_text.FillThisBuffer(omx_buffer);
  }
  return pkt;
}

int COMXVideo::DecodeText(uint8_t *pData, int iSize, double dts, double pts)
{
  OMX_ERRORTYPE omx_err;

  if (pData || iSize > 0)
  {
    unsigned int demuxer_bytes = (unsigned int)iSize;
    uint8_t *demuxer_content = pData;

    while(demuxer_bytes)
    {
      // 10 ms timeout
      OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_text.GetInputBuffer(10);

      if(omx_buffer == NULL)
      {
        CLog::Log(LOGERROR, "OMXVideo::DecodeText timeout\n");
        printf("COMXVideo::DecodeText timeout\n");
        return false;
      }

      omx_buffer->nFlags = 0;

      uint64_t val = (uint64_t)(pts == DVD_NOPTS_VALUE) ? 0 : pts;
      if(m_setStartTimeText)
      {
        omx_buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;
        m_setStartTimeText = false;
      }
      else
      {
        if(pts == DVD_NOPTS_VALUE)
          omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
      }

      omx_buffer->nTimeStamp = ToOMXTime(val);

      omx_buffer->nFilledLen = (demuxer_bytes > (omx_buffer->nAllocLen - 1)) ? (omx_buffer->nAllocLen - 1) : demuxer_bytes;
      memset(omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
      memcpy(omx_buffer->pBuffer, demuxer_content, omx_buffer->nFilledLen);

      /*
      printf("VDec : pts %lld omx_buffer 0x%08x buffer 0x%08x number %d text : %s\n", 
          pts, omx_buffer, omx_buffer->pBuffer, (int)omx_buffer->pAppPrivate, omx_buffer->pBuffer);
      */

      demuxer_bytes -= omx_buffer->nFilledLen;
      demuxer_content += omx_buffer->nFilledLen;

      omx_buffer->nFlags |= OMX_BUFFERFLAG_EOS;

      omx_err = m_omx_text.EmptyThisBuffer(omx_buffer);
      if(omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);

        printf("%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);

        return false;
      }
      if(m_first_text)
      {
        m_omx_text.DisablePort(m_omx_text.GetInputPort(), false);
        m_omx_text.DisablePort(m_omx_text.GetOutputPort(), false);

        m_omx_text.EnablePort(m_omx_text.GetOutputPort(), false);
        m_omx_text.EnablePort(m_omx_text.GetInputPort(), false);

        m_first_text = false;
      }

    }

    return true;

  }
  
  return false;
}

int COMXVideo::Decode(uint8_t *pData, int iSize, double dts, double pts)
{
  OMX_ERRORTYPE omx_err;

  if (pData || iSize > 0)
  {
    unsigned int demuxer_bytes = (unsigned int)iSize;
    uint8_t *demuxer_content = pData;

    if(m_video_convert)
    {
      m_converter->Convert(pData, iSize);
      demuxer_bytes = m_converter->GetConvertSize();
      demuxer_content = m_converter->GetConvertBuffer();
      if(!demuxer_bytes && demuxer_bytes < 1)
      {
        return false;
      }
    }

    while(demuxer_bytes)
    {
      // 500ms timeout
      OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer(500);
      if(omx_buffer == NULL)
      {
        CLog::Log(LOGERROR, "OMXVideo::Decode timeout\n");
        printf("COMXVideo::Decode timeout\n");
        return false;
      }

      /*
      CLog::Log(DEBUG, "COMXVideo::Video VDec : pts %lld omx_buffer 0x%08x buffer 0x%08x number %d\n", 
          pts, omx_buffer, omx_buffer->pBuffer, (int)omx_buffer->pAppPrivate);
      printf("VDec : pts %f omx_buffer 0x%08x buffer 0x%08x number %d\n", 
          (float)pts / AV_TIME_BASE, omx_buffer, omx_buffer->pBuffer, (int)omx_buffer->pAppPrivate);
      */

      omx_buffer->nFlags = 0;
      omx_buffer->nOffset = 0;

      uint64_t val  = (uint64_t)(pts == DVD_NOPTS_VALUE) ? 0 : pts;

      if(m_setStartTime)
      {
        omx_buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;
        m_setStartTime = false;
      }
      else
      {
        if(pts == DVD_NOPTS_VALUE)
          omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
      }

      omx_buffer->nTimeStamp = ToOMXTime(val);

      omx_buffer->nFilledLen = (demuxer_bytes > omx_buffer->nAllocLen) ? omx_buffer->nAllocLen : demuxer_bytes;
      memcpy(omx_buffer->pBuffer, demuxer_content, omx_buffer->nFilledLen);

      demuxer_bytes -= omx_buffer->nFilledLen;
      demuxer_content += omx_buffer->nFilledLen;

      if(demuxer_bytes == 0)
        omx_buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

      int nRetry = 0;
      while(true)
      {
        omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
        if (omx_err == OMX_ErrorNone)
        {
          break;
        }
        else
        {
          CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
          nRetry++;
        }
        if(nRetry == 5)
        {
          CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() finaly failed\n", CLASSNAME, __func__);
          printf("%s::%s - OMX_EmptyThisBuffer() finaly failed\n", CLASSNAME, __func__);
          return false;
        }
      }

      /*
      omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);

      if(omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);

        printf("%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);

        return false;
      }
      */

      if(m_first_frame && m_deinterlace)
      {
        OMX_PARAM_PORTDEFINITIONTYPE port_image;
        OMX_INIT_STRUCTURE(port_image);
        port_image.nPortIndex = m_omx_decoder.GetOutputPort();

        omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_image);
        if(omx_err != OMX_ErrorNone)
        {
          CLog::Log(LOGERROR, "%s::%s - error OMX_IndexParamPortDefinition 1 omx_err(0x%08x)\n", CLASSNAME, __func__, omx_err);
        }

        /* we assume when the sizes equal we have the first decoded frame */
        if(port_image.format.video.nFrameWidth == m_decoded_width && port_image.format.video.nFrameHeight == m_decoded_height)
        {
          m_first_frame = false;

          omx_err = m_omx_decoder.WaitForEvent(OMX_EventPortSettingsChanged);
          if(omx_err == OMX_ErrorStreamCorrupt)
          {
            CLog::Log(LOGERROR, "%s::%s - image not unsupported\n", CLASSNAME, __func__);
            return false;
          }

          m_omx_decoder.DisablePort(m_omx_decoder.GetOutputPort(), false);
          m_omx_sched.DisablePort(m_omx_sched.GetInputPort(), false);

          m_omx_image_fx.DisablePort(m_omx_image_fx.GetOutputPort(), false);
          m_omx_image_fx.DisablePort(m_omx_image_fx.GetInputPort(), false);

          port_image.nPortIndex = m_omx_image_fx.GetInputPort();

          omx_err = m_omx_image_fx.SetParameter(OMX_IndexParamPortDefinition, &port_image);
          if(omx_err != OMX_ErrorNone)
          {
            CLog::Log(LOGERROR, "%s::%s - error OMX_IndexParamPortDefinition 2 omx_err(0x%08x)\n", CLASSNAME, __func__, omx_err);
          }

          port_image.nPortIndex = m_omx_image_fx.GetOutputPort();
          omx_err = m_omx_image_fx.SetParameter(OMX_IndexParamPortDefinition, &port_image);
          if(omx_err != OMX_ErrorNone)
          {
            CLog::Log(LOGERROR, "%s::%s - error OMX_IndexParamPortDefinition 3 omx_err(0x%08x)\n", CLASSNAME, __func__, omx_err);
          }

          m_omx_decoder.EnablePort(m_omx_decoder.GetOutputPort(), false);

          m_omx_image_fx.EnablePort(m_omx_image_fx.GetOutputPort(), false);

          m_omx_image_fx.EnablePort(m_omx_image_fx.GetInputPort(), false);

          m_omx_sched.EnablePort(m_omx_sched.GetInputPort(), false);
        }
      }
    }

    return true;

  }
  
  return false;
}

void COMXVideo::Reset(void)
{
  m_omx_text.FlushAll();
  m_omx_tunnel_text.Flush();
  m_omx_decoder.FlushInput();
  m_omx_tunnel_decoder.Flush();

  //m_setStartTime      = true;
  //m_setStartTimeText  = true;

  //m_first_frame = true;
}

///////////////////////////////////////////////////////////////////////////////////////////
bool COMXVideo::Pause()
{
  if(m_omx_render.GetComponent() == NULL)
    return false;

  if(m_Pause) return true;
  m_Pause = true;

  m_omx_sched.SetStateForComponent(OMX_StatePause);
  m_omx_render.SetStateForComponent(OMX_StatePause);

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////
bool COMXVideo::Resume()
{
  if(m_omx_render.GetComponent() == NULL)
    return false;

  if(!m_Pause) return true;
  m_Pause = false;

  m_omx_sched.SetStateForComponent(OMX_StateExecuting);
  m_omx_render.SetStateForComponent(OMX_StateExecuting);

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////
void COMXVideo::SetVideoRect(const CRect& SrcRect, const CRect& DestRect)
{
  if(!m_is_open)
    return;

  OMX_CONFIG_DISPLAYREGIONTYPE configDisplay;
  OMX_INIT_STRUCTURE(configDisplay);
  configDisplay.nPortIndex = m_omx_render.GetInputPort();

  configDisplay.set     = OMX_DISPLAY_SET_FULLSCREEN;
  configDisplay.fullscreen = OMX_FALSE;

  m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);

  configDisplay.set     = OMX_DISPLAY_SET_DEST_RECT;
  configDisplay.dest_rect.x_offset  = DestRect.x1;
  configDisplay.dest_rect.y_offset  = DestRect.y1;
  configDisplay.dest_rect.width     = DestRect.Width();
  configDisplay.dest_rect.height    = DestRect.Height();

  m_omx_render.SetConfig(OMX_IndexConfigDisplayRegion, &configDisplay);

  printf("dest_rect.x_offset %d dest_rect.y_offset %d dest_rect.width %d dest_rect.height %d\n",
      configDisplay.dest_rect.x_offset, configDisplay.dest_rect.y_offset, 
      configDisplay.dest_rect.width, configDisplay.dest_rect.height);
}

int COMXVideo::GetInputBufferSize()
{
  return m_omx_decoder.GetInputBufferSize();
}

void COMXVideo::WaitCompletion()
{
  if(!m_is_open)
    return;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer();
  struct timespec starttime, endtime;
  
  if(omx_buffer == NULL)
  {
    CLog::Log(LOGERROR, "%s::%s - buffer error 0x%08x", CLASSNAME, __func__, omx_err);
    return;
  }
  
  omx_buffer->nOffset     = 0;
  omx_buffer->nFilledLen  = 0;
  omx_buffer->nTimeStamp  = ToOMXTime(0LL);

  omx_buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;
  
  omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
    return;
  }

  clock_gettime(CLOCK_REALTIME, &starttime);

  while(true)
  {
    if(m_omx_render.IsEOS())
      break;
    clock_gettime(CLOCK_REALTIME, &endtime);
    if((endtime.tv_sec - starttime.tv_sec) > 5)
    {
      CLog::Log(LOGERROR, "%s::%s - wait for eos timed out\n", CLASSNAME, __func__);
      break;
    }
    OMXClock::OMXSleep(50);
  }

  return;
}
