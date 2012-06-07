#pragma once

/*
 *      Copyright (C) 2005-2008 Team XBMC
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

#include "DllAvCodec.h"
#include "DllAvFormat.h"
#include "DllAvUtil.h"

#include "OMXStreamInfo.h"
#include "utils/PCMRemap.h"
#include "linux/PlatformDefs.h"

class COMXAudioCodecOMX
{
public:
  COMXAudioCodecOMX();
  ~COMXAudioCodecOMX();
  bool Open(COMXStreamInfo &hints);
  void Dispose();
  int Decode(BYTE* pData, int iSize);
  int GetData(BYTE** dst);
  void Reset();
  int GetChannels();
  enum PCMChannels *GetChannelMap();
  int GetSampleRate();
  int GetBitsPerSample();
  const char* GetName() { return "FFmpeg"; }
  int GetBufferSize() { return m_iBuffered; }
  int GetBitRate();

protected:
  AVCodecContext* m_pCodecContext;
  AVAudioConvert* m_pConvert;;
  enum AVSampleFormat m_iSampleFormat;
  enum PCMChannels m_channelMap[PCM_MAX_CH + 1];

  AVFrame* m_pFrame1;
  int   m_iBufferSize1;

  BYTE *m_pBuffer2;
  int   m_iBufferSize2;

  bool m_bOpenedCodec;
  int m_iBuffered;

  int     m_channels;
  uint64_t m_layout;

  DllAvCodec m_dllAvCodec;
  DllAvUtil m_dllAvUtil;

  void BuildChannelMap();
};
