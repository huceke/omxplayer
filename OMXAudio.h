/*
* XBMC Media Center
* Copyright (c) 2002 d7o3g4q and RUNTiME
* Portions Copyright (c) by the authors of ffmpeg and xvid
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

//////////////////////////////////////////////////////////////////////

#ifndef __OPENMAXAUDIORENDER_H__
#define __OPENMAXAUDIORENDER_H__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "IAudioRenderer.h"
#include "linux/PlatformDefs.h"
#include "DllAvCodec.h"
#include "DllAvUtil.h"
#include "OMXCore.h"
#include "OMXClock.h"
#include "OMXStreamInfo.h"
#include "BitstreamConverter.h"

class COMXAudio : public IAudioRenderer
{
public:
  unsigned int GetChunkLen();
  float GetDelay();
  float GetCacheTime();
  float GetCacheTotal();
  unsigned int GetAudioRenderingLatency();
  COMXAudio();
  bool Initialize(const CStdString& device, enum PCMChannels *channelMap,
                           COMXStreamInfo &hints, EEncoded bPassthrough, bool bUseHWDecode, bool boostOnDownmix, long initialVolume, float fifo_size);
  bool Initialize(const CStdString& device, int iChannels, enum PCMChannels *channelMap, unsigned int downmixChannels, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool boostOnDownmix, EEncoded bPassthrough = IAudioRenderer::ENCODED_NONE, long initialVolume = 0, float fifo_size = 0);
  ~COMXAudio();

  unsigned int AddPackets(const void* data, unsigned int len);
  unsigned int AddPackets(const void* data, unsigned int len, double dts, double pts);
  unsigned int GetSpace();
  bool Deinitialize();
  bool Pause();
  bool Stop();
  bool Resume();

  long GetCurrentVolume() const;
  void Mute(bool bMute);
  bool SetCurrentVolume(long nVolume);
  int SetPlaySpeed(int iSpeed);
  void SubmitEOS();
  bool IsEOS();

  void Flush();

  void Process();

  bool SetClock(OMXClock *clock);
  void SetCodingType(CodecID codec);
  bool CanHWDecode(CodecID codec);
  static bool HWDecode(CodecID codec);

  void PrintChannels(OMX_AUDIO_CHANNELTYPE eChannelMapping[]);
  void PrintPCM(OMX_AUDIO_PARAM_PCMMODETYPE *pcm);
  void PrintDDP(OMX_AUDIO_PARAM_DDPTYPE *ddparm);
  void PrintDTS(OMX_AUDIO_PARAM_DTSTYPE *dtsparam);
  unsigned int SyncDTS(BYTE* pData, unsigned int iSize);
  unsigned int SyncAC3(BYTE* pData, unsigned int iSize);

private:
  bool          m_Initialized;
  bool          m_Pause;
  bool          m_CanPause;
  long          m_CurrentVolume;
  bool          m_Passthrough;
  bool          m_HWDecode;
  bool          m_normalize_downmix;
  unsigned int  m_BytesPerSec;
  unsigned int  m_BufferLen;
  unsigned int  m_ChunkLen;
  unsigned int  m_InputChannels;
  unsigned int  m_OutputChannels;
  unsigned int  m_downmix_channels;
  unsigned int  m_BitsPerSample;
  COMXCoreComponent *m_omx_clock;
  OMXClock       *m_av_clock;
  bool          m_external_clock;
  bool          m_setStartTime;
  int           m_SampleSize;
  bool          m_first_frame;
  bool          m_LostSync;
  int           m_SampleRate;
  OMX_AUDIO_CODINGTYPE m_eEncoding;
  uint8_t       *m_extradata;
  int           m_extrasize;
  float         m_fifo_size;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_output;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm_input;
  OMX_AUDIO_PARAM_DTSTYPE     m_dtsParam;
  WAVEFORMATEXTENSIBLE        m_wave_header;

protected:
  COMXCoreComponent m_omx_render;
  COMXCoreComponent m_omx_mixer;
  COMXCoreComponent m_omx_decoder;
  COMXCoreTunel     m_omx_tunnel_clock;
  COMXCoreTunel     m_omx_tunnel_mixer;
  COMXCoreTunel     m_omx_tunnel_decoder;
  DllAvUtil         m_dllAvUtil;
};
#endif

