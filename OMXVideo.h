#pragma once
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

#if defined(HAVE_OMXLIB)

#include "OMXCore.h"
#include "OMXStreamInfo.h"

#include <IL/OMX_Video.h>

#include "BitstreamConverter.h"

#include "OMXClock.h"
#include "OMXReader.h"

#include "guilib/Geometry.h"


#define VIDEO_BUFFERS 60

#define CLASSNAME "COMXVideo"

class DllAvUtil;
class DllAvFormat;
class COMXVideo
{
public:
  COMXVideo();
  ~COMXVideo();

  // Required overrides
  bool SendDecoderConfig();
  bool Open(COMXStreamInfo &hints, OMXClock *clock, float display_aspect = 0.0f, bool deinterlace = false, bool hdmi_clock_sync = false, void* boblight_instance = NULL, int boblight_sizedown = 64, int boblight_margin = 10, int boblight_timeout = 35);
  void Close(void);
  unsigned int GetFreeSpace();
  unsigned int GetSize();
  OMXPacket *GetText();
  int  DecodeText(uint8_t *pData, int iSize, double dts, double pts);
  int  Decode(uint8_t *pData, int iSize, double dts, double pts);
  void Reset(void);
  void SetDropState(bool bDrop);
  bool Pause();
  bool Resume();
  std::string GetDecoderName() { return m_video_codec_name; };
  void SetVideoRect(const CRect& SrcRect, const CRect& DestRect);
  int GetInputBufferSize();
  void WaitCompletion();
  static void* BoblightClientThread(void* data);
  static OMX_ERRORTYPE BufferDoneHandler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer);
protected:
  // Video format
  bool              m_drop_state;
  unsigned int      m_decoded_width;
  unsigned int      m_decoded_height;

  OMX_VIDEO_CODINGTYPE m_codingType;

  COMXCoreComponent m_omx_text;
  COMXCoreComponent m_omx_decoder;
  COMXCoreComponent m_omx_render;
  COMXCoreComponent m_omx_sched;
  COMXCoreComponent m_omx_image_fx;
  COMXCoreComponent m_omx_split;
  COMXCoreComponent m_omx_resize;

  static void*      m_boblight; //pointer to boblight instance
  int               m_boblight_sizedown;
  int               m_boblight_margin;
  //internal boblight variables follow
  static unsigned int m_boblight_margin_t; 
  static unsigned int m_boblight_margin_b;
  static unsigned int m_boblight_margin_l;
  static unsigned int m_boblight_margin_r;
  static int        m_boblight_width;
  static int        m_boblight_height;
  static int        m_boblight_timeout;
  static OMX_BUFFERHEADERTYPE*    m_boblight_bufferpointer;
  //boblight threads
  static volatile bool m_boblight_threadstop; //set true to stop all threads

  static pthread_t  m_boblight_clientthread;

  static pthread_mutex_t m_boblight_bufferdone_mutex;
  static pthread_cond_t  m_boblight_bufferdone_cond;
  static volatile bool m_boblight_bufferdone_flag;

  COMXCoreComponent *m_omx_clock;
  OMXClock          *m_av_clock;

  COMXCoreTunel     m_omx_tunnel_text;
  COMXCoreTunel     m_omx_tunnel_decoder;
  COMXCoreTunel     m_omx_tunnel_clock;
  COMXCoreTunel     m_omx_tunnel_sched;
  COMXCoreTunel     m_omx_tunnel_image_fx;
  COMXCoreTunel     m_omx_tunnel_split;
  COMXCoreTunel     m_omx_tunnel_resize;

  bool              m_is_open;

  bool              m_Pause;
  bool              m_setStartTime;
  bool              m_setStartTimeText;

  uint8_t           *m_extradata;
  int               m_extrasize;

  CBitstreamConverter   *m_converter;
  bool              m_video_convert;
  std::string       m_video_codec_name;

  bool              m_deinterlace;
  bool              m_hdmi_clock_sync;
  bool              m_first_frame;
  bool              m_first_text;
};

#endif
