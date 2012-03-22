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

#include "OMXAudioCodecOMX.h"
#ifdef _LINUX
#include "XMemUtils.h"
#endif
#include "utils/log.h"

#define MAX_AUDIO_FRAME_SIZE (AVCODEC_MAX_AUDIO_FRAME_SIZE*1.5)

COMXAudioCodecOMX::COMXAudioCodecOMX()
{
  m_iBufferSize1 = 0;
  m_pBuffer1     = (BYTE*)_aligned_malloc(MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE, 16);
  memset(m_pBuffer1, 0, MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);

  m_iBufferSize2 = 0;
  m_pBuffer2     = (BYTE*)_aligned_malloc(MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE, 16);
  memset(m_pBuffer2, 0, MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);

  m_iBuffered = 0;
  m_pCodecContext = NULL;
  m_pConvert = NULL;
  m_bOpenedCodec = false;

  m_channelMap[0] = PCM_INVALID;
  m_channels = 0;
  m_layout = 0;
}

COMXAudioCodecOMX::~COMXAudioCodecOMX()
{
  _aligned_free(m_pBuffer1);
  _aligned_free(m_pBuffer2);

  Dispose();
}

bool COMXAudioCodecOMX::Open(COMXStreamInfo &hints)
{
  AVCodec* pCodec;
  m_bOpenedCodec = false;

  if (!m_dllAvCore.Load() || !m_dllAvUtil.Load() || !m_dllAvCodec.Load())
    return false;

  m_dllAvCodec.avcodec_register_all();
  m_pCodecContext = m_dllAvCodec.avcodec_alloc_context();
  m_dllAvCodec.avcodec_get_context_defaults(m_pCodecContext);

  pCodec = m_dllAvCodec.avcodec_find_decoder(hints.codec);
  if (!pCodec)
  {
    CLog::Log(LOGDEBUG,"COMXAudioCodecOMX::Open() Unable to find codec %d", hints.codec);
    return false;
  }

  m_pCodecContext->debug_mv = 0;
  m_pCodecContext->debug = 0;
  m_pCodecContext->workaround_bugs = 1;

  if (pCodec->capabilities & CODEC_CAP_TRUNCATED)
    m_pCodecContext->flags |= CODEC_FLAG_TRUNCATED;

  m_channels = 0;
  m_pCodecContext->channels = hints.channels;
  m_pCodecContext->sample_rate = hints.samplerate;
  m_pCodecContext->block_align = hints.blockalign;
  m_pCodecContext->bit_rate = hints.bitrate;
  m_pCodecContext->bits_per_coded_sample = hints.bitspersample;

  if(m_pCodecContext->bits_per_coded_sample == 0)
    m_pCodecContext->bits_per_coded_sample = 16;

  if( hints.extradata && hints.extrasize > 0 )
  {
    m_pCodecContext->extradata_size = hints.extrasize;
    m_pCodecContext->extradata = (uint8_t*)m_dllAvUtil.av_mallocz(hints.extrasize + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(m_pCodecContext->extradata, hints.extradata, hints.extrasize);
  }

  if (m_dllAvCodec.avcodec_open(m_pCodecContext, pCodec) < 0)
  {
    CLog::Log(LOGDEBUG,"COMXAudioCodecOMX::Open() Unable to open codec");
    Dispose();
    return false;
  }

  m_bOpenedCodec = true;
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  return true;
}

void COMXAudioCodecOMX::Dispose()
{
  if (m_pConvert)
  {
    m_dllAvCodec.av_audio_convert_free(m_pConvert);
    m_pConvert = NULL;

  }

  if (m_pCodecContext)
  {
    if (m_bOpenedCodec) m_dllAvCodec.avcodec_close(m_pCodecContext);
    m_bOpenedCodec = false;
    m_dllAvUtil.av_free(m_pCodecContext);
    m_pCodecContext = NULL;
  }

  m_dllAvCodec.Unload();
  m_dllAvUtil.Unload();

  m_iBufferSize1 = 0;
  m_iBufferSize2 = 0;
  m_iBuffered = 0;
}

int COMXAudioCodecOMX::Decode(BYTE* pData, int iSize)
{
  int iBytesUsed;
  if (!m_pCodecContext) return -1;
  if (iSize < 1) return iSize;

  m_iBufferSize1 = AVCODEC_MAX_AUDIO_FRAME_SIZE;
  m_iBufferSize2 = 0;

  AVPacket avpkt;
  m_dllAvCodec.av_init_packet(&avpkt);
  avpkt.data = pData;
  avpkt.size = iSize;
  iBytesUsed = m_dllAvCodec.avcodec_decode_audio3( m_pCodecContext
                                                 , (int16_t*)m_pBuffer1
                                                 , &m_iBufferSize1
                                                 , &avpkt);

  /* some codecs will attempt to consume more data than what we gave */
  if (iBytesUsed > iSize)
  {
    CLog::Log(LOGWARNING, "COMXAudioCodecOMX::Decode - decoder attempted to consume more data than given");
    iBytesUsed = iSize;
  }

  if(m_iBufferSize1 == 0 && iBytesUsed >= 0)
    m_iBuffered += iBytesUsed;
  else
    m_iBuffered = 0;

  if(m_pCodecContext->sample_fmt != AV_SAMPLE_FMT_S16 && m_iBufferSize1 > 0)
  {
    if(m_pConvert && m_pCodecContext->sample_fmt != m_iSampleFormat)
    {
      m_dllAvCodec.av_audio_convert_free(m_pConvert);
      m_pConvert = NULL;
    }

    if(!m_pConvert)
    {
      m_iSampleFormat = m_pCodecContext->sample_fmt;
      m_pConvert = m_dllAvCodec.av_audio_convert_alloc(AV_SAMPLE_FMT_S16, 1, m_pCodecContext->sample_fmt, 1, NULL, 0);
    }

    if(!m_pConvert)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode - Unable to convert %d to AV_SAMPLE_FMT_S16", m_pCodecContext->sample_fmt);
      m_iBufferSize1 = 0;
      m_iBufferSize2 = 0;
      return iBytesUsed;
    }

    const void *ibuf[6] = { m_pBuffer1 };
    void       *obuf[6] = { m_pBuffer2 };
    int         istr[6] = { m_dllAvCore.av_get_bits_per_sample_fmt(m_pCodecContext->sample_fmt)/8 };
    int         ostr[6] = { 2 };
    int         len     = m_iBufferSize1 / istr[0];
    if(m_dllAvCodec.av_audio_convert(m_pConvert, obuf, ostr, ibuf, istr, len) < 0)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode - Unable to convert %d to AV_SAMPLE_FMT_S16", (int)m_pCodecContext->sample_fmt);
      m_iBufferSize1 = 0;
      m_iBufferSize2 = 0;
      return iBytesUsed;
    }

    m_iBufferSize1 = 0;
    m_iBufferSize2 = len * ostr[0];
  }

  return iBytesUsed;
}

int COMXAudioCodecOMX::GetData(BYTE** dst)
{
  // TODO: Use a third buffer and decide which is our source data
  if(m_pCodecContext->channels == 6 && m_iBufferSize1)
  {
    int16_t *pDst = (int16_t *)m_pBuffer2;
    int16_t *pSrc = (int16_t *)m_pBuffer1;

    //printf("\ncopy_chunk_len %d, omx_chunk_len %d\n", copy_chunk_len, omx_chunk_len);
    memset(m_pBuffer2, 0, MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);

    m_iBufferSize2 = 0;
    int size = m_iBufferSize1 / 2;
    int gap = 8 - m_pCodecContext->channels;
    int samples = 0;

    for(int i = 0; i < size; pDst++, pSrc++, i++, samples++)
    {
      if( (i%m_pCodecContext->channels) == 0)
      {
        pDst    +=  gap;
        samples +=  gap;
      }

      *pDst = *pSrc;
    }

    m_iBufferSize2 = samples * 2;

    *dst = m_pBuffer2;
    return m_iBufferSize2;
  }

  if(m_iBufferSize1)
  {
    *dst = m_pBuffer1;
    return m_iBufferSize1;
  }
  if(m_iBufferSize2)
  {
    *dst = m_pBuffer2;
    return m_iBufferSize2;
  }
  return 0;
}

void COMXAudioCodecOMX::Reset()
{
  if (m_pCodecContext) m_dllAvCodec.avcodec_flush_buffers(m_pCodecContext);
  m_iBufferSize1 = 0;
  m_iBufferSize2 = 0;
  m_iBuffered = 0;
}

int COMXAudioCodecOMX::GetChannels()
{
  return (m_pCodecContext->channels == 6) ? 8 : m_pCodecContext->channels;
}

int COMXAudioCodecOMX::GetSampleRate()
{
  if (m_pCodecContext) return m_pCodecContext->sample_rate;
  return 0;
}

int COMXAudioCodecOMX::GetBitsPerSample()
{
  return 16;
}

int COMXAudioCodecOMX::GetBitRate()
{
  if (m_pCodecContext) return m_pCodecContext->bit_rate;
  return 0;
}

static unsigned count_bits(int64_t value)
{
  unsigned bits = 0;
  for(;value;++bits)
    value &= value - 1;
  return bits;
}

void COMXAudioCodecOMX::BuildChannelMap()
{
  if (m_channels == m_pCodecContext->channels && m_layout == m_pCodecContext->channel_layout)
    return; //nothing to do here

  m_channels = m_pCodecContext->channels;
  m_layout   = m_pCodecContext->channel_layout;

  int64_t layout;

  int bits = count_bits(m_pCodecContext->channel_layout);
  if (bits == m_pCodecContext->channels)
    layout = m_pCodecContext->channel_layout;
  else
  {
    CLog::Log(LOGINFO, "COMXAudioCodecOMX::GetChannelMap - FFmpeg reported %d channels, but the layout contains %d ignoring", m_pCodecContext->channels, bits);
    layout = m_dllAvCodec.avcodec_guess_channel_layout(m_pCodecContext->channels, m_pCodecContext->codec_id, NULL);
  }

  int index = 0;
  if (layout & AV_CH_FRONT_LEFT           ) m_channelMap[index++] = PCM_FRONT_LEFT           ;
  if (layout & AV_CH_FRONT_RIGHT          ) m_channelMap[index++] = PCM_FRONT_RIGHT          ;
  if (layout & AV_CH_FRONT_CENTER         ) m_channelMap[index++] = PCM_FRONT_CENTER         ;
  if (layout & AV_CH_LOW_FREQUENCY        ) m_channelMap[index++] = PCM_LOW_FREQUENCY        ;
  if (layout & AV_CH_BACK_LEFT            ) m_channelMap[index++] = PCM_BACK_LEFT            ;
  if (layout & AV_CH_BACK_RIGHT           ) m_channelMap[index++] = PCM_BACK_RIGHT           ;
  if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) m_channelMap[index++] = PCM_FRONT_LEFT_OF_CENTER ;
  if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) m_channelMap[index++] = PCM_FRONT_RIGHT_OF_CENTER;
  if (layout & AV_CH_BACK_CENTER          ) m_channelMap[index++] = PCM_BACK_CENTER          ;
  if (layout & AV_CH_SIDE_LEFT            ) m_channelMap[index++] = PCM_SIDE_LEFT            ;
  if (layout & AV_CH_SIDE_RIGHT           ) m_channelMap[index++] = PCM_SIDE_RIGHT           ;
  if (layout & AV_CH_TOP_CENTER           ) m_channelMap[index++] = PCM_TOP_CENTER           ;
  if (layout & AV_CH_TOP_FRONT_LEFT       ) m_channelMap[index++] = PCM_TOP_FRONT_LEFT       ;
  if (layout & AV_CH_TOP_FRONT_CENTER     ) m_channelMap[index++] = PCM_TOP_FRONT_CENTER     ;
  if (layout & AV_CH_TOP_FRONT_RIGHT      ) m_channelMap[index++] = PCM_TOP_FRONT_RIGHT      ;
  if (layout & AV_CH_TOP_BACK_LEFT        ) m_channelMap[index++] = PCM_TOP_BACK_LEFT        ;
  if (layout & AV_CH_TOP_BACK_CENTER      ) m_channelMap[index++] = PCM_TOP_BACK_CENTER      ;
  if (layout & AV_CH_TOP_BACK_RIGHT       ) m_channelMap[index++] = PCM_TOP_BACK_RIGHT       ;

  //terminate the channel map
  m_channelMap[index] = PCM_INVALID;
  if(m_pCodecContext->channels == 6)
  {
    m_channelMap[6] = PCM_INVALID;
    m_channelMap[7] = PCM_INVALID;
    m_channelMap[8] = PCM_INVALID;
  }
}

enum PCMChannels* COMXAudioCodecOMX::GetChannelMap()
{
  BuildChannelMap();

  if (m_channelMap[0] == PCM_INVALID)
    return NULL;

  return m_channelMap;
}
