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

#ifndef _OMX_READER_H_
#define _OMX_READER_H_

#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvFilter.h"
#include "DllAvCodec.h"
#include "OMXStreamInfo.h"
#ifdef STANDALONE
#include "OMXThread.h"
#else
#include "threads/Thread.h"
#endif
#include <queue>

#include "OMXStreamInfo.h"

#ifdef STANDALONE
#include "File.h"
#else
#include "xbmc/filesystem/File.h"
#endif

#include <sys/types.h>
#include <string>

using namespace XFILE;
using namespace std;

#define MAX_OMX_CHAPTERS 64

#define MAX_OMX_STREAMS        100

#define OMX_PLAYSPEED_PAUSE  0
#define OMX_PLAYSPEED_NORMAL 1

#ifndef FFMPEG_FILE_BUFFER_SIZE
#define FFMPEG_FILE_BUFFER_SIZE   32768 // default reading size for ffmpeg
#endif
#ifndef MAX_STREAMS
#define MAX_STREAMS 100
#endif

typedef struct OMXChapter
{
  std::string name;
  int64_t     seekto_ms;
  double      ts;
} OMXChapter;

class OMXReader;

typedef struct OMXPacket
{
  double    pts; // pts in DVD_TIME_BASE
  double    dts; // dts in DVD_TIME_BASE
  double    now; // dts in DVD_TIME_BASE
  double    duration; // duration in DVD_TIME_BASE if available
  int       size;
  uint8_t   *data;
  int       stream_index;
  COMXStreamInfo hints;
  enum AVMediaType codec_type;
} OMXPacket;

enum OMXStreamType
{
  OMXSTREAM_NONE      = 0,
  OMXSTREAM_AUDIO     = 1,
  OMXSTREAM_VIDEO     = 2,
  OMXSTREAM_SUBTITLE  = 3
};

typedef struct OMXStream
{
  char language[4];
  std::string name;
  std::string codec_name;
  AVStream    *stream;
  OMXStreamType type;
  int         id;
  void        *extradata;
  unsigned int extrasize;
  unsigned int index;
  COMXStreamInfo hints;
} OMXStream;

class OMXReader
{
protected:
  int                       m_video_index;
  int                       m_audio_index;
  int                       m_subtitle_index;
  int                       m_video_count;
  int                       m_audio_count;
  int                       m_subtitle_count;
  DllAvUtil                 m_dllAvUtil;
  DllAvCodec                m_dllAvCodec;
  DllAvFormat               m_dllAvFormat;
  bool                      m_open;
  std::string               m_filename;
  bool                      m_bMatroska;
  bool                      m_bAVI;
  bool                      m_bMpeg;
  XFILE::CFile              *m_pFile;
  AVFormatContext           *m_pFormatContext;
  AVIOContext               *m_ioContext;
  bool                      m_eof;
  OMXChapter                m_chapters[MAX_OMX_CHAPTERS];
  OMXStream                 m_streams[MAX_STREAMS];
  int                       m_chapter_count;
  double                    m_iCurrentPts;
  int                       m_speed;
  unsigned int              m_program;
//#ifdef STANDALONE
//  void flush_packet_queue(AVFormatContext *s);
//  void av_read_frame_flush(AVFormatContext *s);
//#endif
  pthread_mutex_t           m_lock;
  void Lock();
  void UnLock();
  bool SetActiveStreamInternal(OMXStreamType type, unsigned int index);
  bool                      m_seek;
private:
public:
  OMXReader();
  ~OMXReader();
  bool Open(std::string filename, bool dump_format);
  void ClearStreams();
  bool Close();
  //void FlushRead();
  bool SeekTime(int64_t seek_ms, int seek_flags, double *startpts);
  AVMediaType PacketType(OMXPacket *pkt);
  OMXPacket *Read();
  void Process();
  bool GetStreams();
  void AddStream(int id);
  bool IsActive(int stream_index);
  bool IsActive(OMXStreamType type, int stream_index);
  bool GetHints(AVStream *stream, COMXStreamInfo *hints);
  bool GetHints(OMXStreamType type, unsigned int index, COMXStreamInfo &hints);
  bool GetHints(OMXStreamType type, COMXStreamInfo &hints);
  bool IsEof();
  int  AudioStreamCount() { return m_audio_count; };
  int  VideoStreamCount() { return m_video_count; };
  int  SubtitleStreamCount() { return m_subtitle_count; };
  bool SetActiveStream(OMXStreamType type, unsigned int index);
  int  GetChapterCount() { return m_chapter_count; };
  OMXChapter GetChapter(unsigned int chapter) { return m_chapters[(chapter > MAX_OMX_CHAPTERS) ? MAX_OMX_CHAPTERS : chapter]; };
  static void FreePacket(OMXPacket *pkt);
  static OMXPacket *AllocPacket(int size);
  void SetSpeed(int iSpeed);
  void UpdateCurrentPTS();
  double ConvertTimestamp(int64_t pts, int den, int num);
  double ConvertTimestamp(int64_t pts, AVRational *time_base);
  int GetChapter();
  void GetChapterName(std::string& strChapterName);
  bool SeekChapter(int chapter, double* startpts);
  int GetAudioIndex() { return (m_audio_index >= 0) ? m_streams[m_audio_index].index : -1; };
  int GetSubtitleIndex() { return (m_subtitle_index >= 0) ? m_streams[m_subtitle_index].index : -1; };
  
  int GetRelativeIndex(size_t index)
  {
    assert(index < MAX_STREAMS);
    return m_streams[index].index;
  }
  
  int GetStreamLength();
  static double NormalizeFrameduration(double frameduration);
  bool IsMpegVideo() { return m_bMpeg; };
  bool IsMatroska() { return m_bMatroska; };
  std::string GetCodecName(OMXStreamType type);
  std::string GetCodecName(OMXStreamType type, unsigned int index);
  std::string GetStreamCodecName(AVStream *stream);
  std::string GetStreamLanguage(OMXStreamType type, unsigned int index);
  std::string GetStreamName(OMXStreamType type, unsigned int index);
  std::string GetStreamType(OMXStreamType type, unsigned int index);
  bool CanSeek();
#ifndef STANDALONE
  int GetSourceBitrate();
#endif
};
#endif
