/*
 * 
 *      Copyright (C) 2012 Edgar Hucek
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

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <termios.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <getopt.h> 

#define AV_NOWARN_DEPRECATED

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
};

#include "OMXStreamInfo.h"

#include "utils/log.h"

#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvFilter.h"
#include "DllAvCodec.h"
#include "DllAvCore.h"
#include "linux/RBP.h"

#include "OMXVideo.h"
#include "OMXAudioCodecOMX.h"
#include "utils/PCMRemap.h"
#include "OMXClock.h"
#include "OMXAudio.h"
#include "OMXReader.h"
#include "OMXPlayerVideo.h"
#include "OMXPlayerAudio.h"
#include "DllOMX.h"

enum PCMChannels  *m_pChannelMap        = NULL;
unsigned int      g_abort               = false;
bool              m_bMpeg               = false;
bool               m_passthrough        = false;
bool              m_Deinterlace         = false;
bool              m_HWDecode            = false;
CStdString        deviceString          = "omx:local";
int               m_use_hw_audio        = false;
bool              m_Pause               = false;
OMXReader         m_omx_reader;
int               m_audio_index_use     = -1;
bool              m_buffer_empty        = true;
bool              m_thread_player       = false;
int64_t           m_audio_offset_ms     = 0;
OMXClock          *m_av_clock           = NULL;
COMXStreamInfo    m_hints_audio;
COMXStreamInfo    m_hints_video;
OMXPacket         *m_omx_pkt            = NULL;
bool              m_hdmi_clock_sync     = false;
bool              m_stop                = false;
bool              m_show_subtitle       = false;
bool              m_subtitle_index      = 0;
DllBcmHost        m_BcmHost;
OMXPlayerVideo    m_player_video;
OMXPlayerAudio    m_player_audio;
int               m_tv_show_info        = 0;
bool              m_has_video           = false;
bool              m_has_audio           = false;
bool              m_has_subtitle        = false;

enum{ERROR=-1,SUCCESS,ONEBYTE};

static struct termios orig_termios;
static void restore_termios (int status, void * arg)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void sig_handler(int s)
{
  printf("strg-c catched\n");
  g_abort = true;
}

void print_usage()
{
  printf("Usage: omxplayer [OPTIONS] [FILE]\n");
  printf("Options :\n");
  printf("         -h / --help                    print this help\n");
  printf("         -a / --alang language          audio language        : e.g. ger\n");
  printf("         -n / --aidx  index             audio stream index    : e.g. 1\n");
  printf("         -o / --adev  device            audio out device      : e.g. hdmi/local\n");
  printf("         -i / --info                    dump stream format and exit\n");
  printf("         -s / --stats                   pts and buffer stats\n");
  printf("         -p / --passthrough             audio passthrough\n");
  printf("         -d / --deinterlace             deinterlacing\n");
  printf("         -w / --hw                      hw audio decoding\n");
  printf("         -3 / --3d                      switch tv into 3d mode\n");
  printf("         -y / --hdmiclocksync           adjust display refresh rate to match video\n");
  printf("         -t / --sid index               show subtitle with index\n");
}

void SetSpeed(int iSpeed)
{
  if(!m_av_clock)
    return;

  if(iSpeed < OMX_PLAYSPEED_PAUSE)
    return;

  m_omx_reader.SetSpeed(iSpeed);

  if(m_av_clock->OMXPlaySpeed() != OMX_PLAYSPEED_PAUSE && iSpeed == OMX_PLAYSPEED_PAUSE)
    m_Pause = true;
  else if(m_av_clock->OMXPlaySpeed() == OMX_PLAYSPEED_PAUSE && iSpeed != OMX_PLAYSPEED_PAUSE)
    m_Pause = false;

  m_av_clock->OMXSpeed(iSpeed);
}

void FlushStreams()
{
  if(m_av_clock)
    m_av_clock->OMXPause();

  if(m_has_video)
    m_player_video.Flush();

  if(m_has_audio)
    m_player_audio.Flush();

  if(m_omx_pkt)
  {
    m_omx_reader.FreePacket(m_omx_pkt);
    m_omx_pkt = NULL;
  }

  if(m_av_clock)
  {
    m_av_clock->OMXReset();
    m_av_clock->OMXResume();
  }
}

int main(int argc, char *argv[])
{
  struct termios new_termios;

  tcgetattr(STDIN_FILENO, &orig_termios);

  new_termios             = orig_termios;
  new_termios.c_lflag     &= ~(ICANON | ECHO | ECHOCTL | ECHONL);
  new_termios.c_cflag     |= HUPCL;
  new_termios.c_cc[VMIN]  = 0;

  CStdString last_sub = "";

  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
  on_exit(restore_termios, &orig_termios);

  CStdString            m_filename;
  double                m_incr                = 0;
  CRBP                  g_RBP;
  COMXCore              g_OMX;
  bool                  m_stats               = false;
  bool                  m_dump_format         = false;
  bool                  m_3d                  = false;
  double                startpts              = 0;
  TV_GET_STATE_RESP_T   tv_state;

  struct option longopts[] = {
    { "info",         no_argument,        NULL,          'i' },
    { "help",         no_argument,        NULL,          'h' },
    { "aidx",         required_argument,  NULL,          'n' },
    { "adev",         required_argument,  NULL,          'o' },
    { "stats",        no_argument,        NULL,          's' },
    { "passthrough",  no_argument,        NULL,          'p' },
    { "deinterlace",  no_argument,        NULL,          'd' },
    { "hw",           no_argument,        NULL,          'w' },
    { "3d",           no_argument,        NULL,          '3' },
    { "hdmiclocksync", no_argument,       NULL,          'y' },
    { "sid",          required_argument,  NULL,          't' },
    { 0, 0, 0, 0 }
  };

  int c;
  while ((c = getopt_long(argc, argv, "wihn:o:cslpd3yt:", longopts, NULL)) != -1)  
  {
    switch (c) 
    {
      case 'y':
        m_hdmi_clock_sync = true;
        break;
      case '3':
        m_3d = true;
        break;
      case 'd':
        m_Deinterlace = true;
        break;
      case 'w':
        m_use_hw_audio = true;
        break;
      case 'p':
        m_passthrough = true;
        break;
      case 's':
        m_stats = true;
        break;
      case 'o':
        deviceString = optarg;
        if(deviceString != CStdString("local") && deviceString != CStdString("hdmi"))
        {
          print_usage();
          return 0;
        }
        deviceString = "omx:" + deviceString;
        break;
      case 'i':
        m_dump_format = true;
        break;
      case 't':
        m_subtitle_index = atoi(optarg) - 1;
        if(m_subtitle_index < 0)
          m_subtitle_index = 0;
        m_show_subtitle = true;
        break;
      case 'n':
        m_audio_index_use = atoi(optarg) - 1;
        if(m_audio_index_use < 0)
          m_audio_index_use = 0;
        break;
      case 0:
        break;
      case 'h':
        print_usage();
        return 0;
        break;
      case ':':
        return 0;
        break;
      default:
        return 0;
        break;
    }
  }

  if (optind >= argc) {
    print_usage();
    return 0;
  }

  m_filename = argv[optind];

  CLog::Init("./");

  g_RBP.Initialize();
  g_OMX.Initialize();

  m_av_clock = new OMXClock();

  m_thread_player = true;

  if(!m_omx_reader.Open(m_filename.c_str(), m_dump_format))
    goto do_exit;

  if(m_dump_format)
    goto do_exit;

  m_bMpeg         = m_omx_reader.IsMpegVideo();
  m_has_video     = m_omx_reader.VideoStreamCount();
  m_has_audio     = m_omx_reader.AudioStreamCount();
  m_has_subtitle  = m_omx_reader.SubtitleStreamCount();

  if(!m_av_clock->OMXInitialize(m_has_video, m_has_audio))
    goto do_exit;

  if(m_hdmi_clock_sync && !m_av_clock->HDMIClockSync())
      goto do_exit;

  m_omx_reader.GetHints(OMXSTREAM_AUDIO, m_hints_audio);
  m_omx_reader.GetHints(OMXSTREAM_VIDEO, m_hints_video);

  if(m_audio_index_use != -1)
    m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, m_audio_index_use);
          
  if(m_has_video && !m_player_video.Open(m_hints_video, m_av_clock, m_Deinterlace,  m_bMpeg, 
                                         m_has_audio, m_hdmi_clock_sync, m_thread_player))
    goto do_exit;

  if(m_has_video)
  {
    int width   = 1280;
    int height  = 720;

    if(m_hints_video.width <= 720)
    {
      width = 720; height = 576;
    }
    else if(m_hints_video.width <= 1280)
    {
      width = 1280; height = 720;
    }
    else
    {
      width = 1920; height = 1080;
    }

    memset(&tv_state, 0, sizeof(TV_GET_STATE_RESP_T));
    m_BcmHost.vc_tv_get_state(&tv_state);

    int32_t num_modes;
    HDMI_RES_GROUP_T prefer_group;
    int i = 0;
    uint32_t prefer_mode;
    #define TV_MAX_SUPPORTED_MODES 60
    TV_SUPPORTED_MODE_T supported_modes[TV_MAX_SUPPORTED_MODES];
    uint32_t group = HDMI_RES_GROUP_CEA;
    int mode = 1;


    if(m_filename.find("3DSBS") != string::npos)
      m_3d = true;

    if(m_3d)
    {
      group = HDMI_RES_GROUP_CEA_3D;
    }
    else
    {
      group = HDMI_RES_GROUP_CEA;
    }

    num_modes = m_BcmHost.vc_tv_hdmi_get_supported_modes((HDMI_RES_GROUP_T)group,
                                           supported_modes,
                                           TV_MAX_SUPPORTED_MODES,
                                           &prefer_group,
                                           &prefer_mode);

    float last_diff = (float)m_player_video.GetFPS();
    if(m_3d)
      last_diff *= 2;

    TV_SUPPORTED_MODE_T *tv_found = NULL;
    if (num_modes > 0 && prefer_group != HDMI_RES_GROUP_INVALID)
    {
      TV_SUPPORTED_MODE_T *tv = supported_modes;
      for (i=0; i<num_modes; i++, tv++)
      {
        float fps = ((group==HDMI_RES_GROUP_CEA_3D) ? (float)m_player_video.GetFPS() * 2.0f : (float)m_player_video.GetFPS());
        
        if(tv->width == width && tv->height == height && fps <= (float)tv->frame_rate)
        {
          float diff = (float)tv->frame_rate - fps;
          if(diff < 0.0f)
            diff *= -1.0f;

          if(diff < last_diff)
          {
            last_diff = diff;
            tv_found = tv;
            mode = supported_modes[i].code;
          }
        } 
        else if( fps <= (float)tv->frame_rate && tv->width >= width && tv->height >= height)
        {
          float diff = (float)tv->frame_rate - fps;
          if(diff < 0.0f)
            diff *= -1.0f;

          if(diff < last_diff)
          {
            last_diff = diff;
            tv_found = tv;
            mode = supported_modes[i].code;
          }
        }
      }

      if(!tv_found)
      {
        tv = supported_modes;
        for (i=0; i<num_modes; i++, tv++)
        {
          if(tv->width == width && tv->height == height)
          {
            tv_found = tv;
            mode = supported_modes[i].code;
            break;
          }
        }
      }
      if(tv_found)
        printf("Output mode %d: %dx%d@%d %s%s:%x\n", i, tv_found->width, tv_found->height, 
               tv_found->frame_rate, tv_found->native?"N":"", tv_found->scan_mode?"I":"", tv_found->code);
    }

    if(tv_found)
      m_BcmHost.vc_tv_hdmi_power_on_explicit(HDMI_MODE_HDMI, (HDMI_RES_GROUP_T)group, mode);

  }

  m_av_clock->OMXStateExecute();
  m_av_clock->SetSpeed(DVD_PLAYSPEED_NORMAL);

  if(m_has_subtitle && m_subtitle_index > (m_omx_reader.SubtitleStreamCount() - 1))
  {
    m_subtitle_index = m_omx_reader.SubtitleStreamCount() - 1;
    m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, m_subtitle_index);
    m_show_subtitle = true;
  }
  else
  {
    m_show_subtitle = false;
  }

  m_omx_reader.GetHints(OMXSTREAM_AUDIO, m_hints_audio);

  if(m_has_audio && !m_player_audio.Open(m_hints_audio, m_av_clock, &m_omx_reader, deviceString, 
                                         m_passthrough, m_use_hw_audio, m_thread_player))
    goto do_exit;

  m_av_clock->OMXStateExecute();
  m_av_clock->OMXReset();
  m_av_clock->OMXResume();

  struct timespec starttime, endtime;

  while(!m_stop)
  {
    int ch[8];
    int chnum = 0;

    if(g_abort)
      goto do_exit;
    
    while((ch[chnum] = getchar()) != EOF) chnum++;
    if (chnum > 1) ch[0] = ch[chnum - 1] | (ch[chnum - 2] << 8);

    switch(ch[0])
    {
      case 'z':
        m_tv_show_info = !m_tv_show_info;
        vc_tv_show_info(m_tv_show_info);
        break;
      case '1':
        SetSpeed(m_av_clock->OMXPlaySpeed() - 1);
        break;
      case '2':
        SetSpeed(m_av_clock->OMXPlaySpeed() + 1);
        break;
      case 'j':
        m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, m_omx_reader.GetAudioIndex() - 1);
        break;
      case 'k':
        m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, m_omx_reader.GetAudioIndex() + 1);
        break;
      case 'i':
        if(m_omx_reader.GetChapterCount() > 0)
        {
          m_omx_reader.SeekChapter(m_omx_reader.GetChapter() - 1, &startpts);
          FlushStreams();
        }
        else
        {
          m_incr = -600.0;
        }
        break;
      case 'o':
        if(m_omx_reader.GetChapterCount() > 0)
        {
          m_omx_reader.SeekChapter(m_omx_reader.GetChapter() + 1, &startpts);
          FlushStreams();
        }
        else
        {
          m_incr = 600.0;
        }
        break;
      case 'n':
        if(m_omx_reader.GetSubtitleIndex() > 0)
        {
          m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, m_omx_reader.GetSubtitleIndex() - 1);
          m_player_video.FlushSubtitles();
        }
        break;
      case 'm':
        if(m_omx_reader.GetSubtitleIndex() > 0)
        {
          m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, m_omx_reader.GetSubtitleIndex() + 1);
          m_player_video.FlushSubtitles();
        }
        break;
      case 's':
        m_show_subtitle = !m_show_subtitle;
        break;
      case 'q':
        m_stop = true;
        goto do_exit;
        break;
      case 0x5b44: // key left
        m_incr = -30.0;
        break;
      case 0x5b43: // key right
        m_incr = 30.0;
        break;
      case 0x5b41: // key up
        m_incr = 600.0;
        break;
      case 0x5b42: // key down
        m_incr = -600.0;
        break;
      case ' ':
      case 'p':
        m_Pause = !m_Pause;
        if(m_Pause)
        {
          SetSpeed(OMX_PLAYSPEED_PAUSE);
          m_av_clock->OMXPause();
        }
        else
        {
          SetSpeed(OMX_PLAYSPEED_NORMAL);
          m_av_clock->OMXResume();
        }
        break;
      default:
        break;
    }

    if(m_Pause)
    {
      OMXClock::OMXSleep(2);
      continue;
    }

    if(m_incr != 0 && !m_bMpeg)
    {
      int    seek_flags   = 0;
      double seek_pos     = 0;
      double pts          = 0;

      pts = m_av_clock->GetPTS();

      seek_pos = (pts / DVD_TIME_BASE) + m_incr;
      seek_flags = m_incr < 0 ? AVSEEK_FLAG_BACKWARD : 0;

      if(seek_pos < 0)
        seek_pos = 0;

      seek_pos *= 1000;

      m_incr = 0;

      if(m_omx_reader.SeekTime(seek_pos, seek_flags, &startpts))
        FlushStreams();
    }

    /* when the audio buffer runs under 0.1 seconds we buffer up */
    if(m_has_audio)
    {
      if(m_player_audio.GetDelay() < 0.1f && !m_buffer_empty)
      {
        if(!m_av_clock->OMXIsPaused())
        {
          m_av_clock->OMXPause();
          //printf("buffering start\n");
          m_buffer_empty = true;
          clock_gettime(CLOCK_REALTIME, &starttime);
        }
      }
      if(m_player_audio.GetDelay() > (AUDIO_BUFFER_SECONDS * 0.75f) && m_buffer_empty)
      {
        if(m_av_clock->OMXIsPaused())
        {
          m_av_clock->OMXResume();
          //printf("buffering end\n");
          m_buffer_empty = false;
        }
      }
      if(m_buffer_empty)
      {
        clock_gettime(CLOCK_REALTIME, &endtime);
        if((endtime.tv_sec - starttime.tv_sec) > 1)
        {
          m_buffer_empty = false;
          m_av_clock->OMXResume();
          //printf("buffering timed out\n");
        }
      }
    }

    if(!m_omx_pkt)
      m_omx_pkt = m_omx_reader.Read();

    if(m_omx_pkt && m_omx_reader.IsActive(OMXSTREAM_VIDEO, m_omx_pkt->stream_index))
    {
      if(m_omx_pkt->pts != DVD_NOPTS_VALUE)
        m_omx_pkt->pts += (m_audio_offset_ms * 1000);
      if(m_omx_pkt->dts != DVD_NOPTS_VALUE)
        m_omx_pkt->dts += (m_audio_offset_ms * 1000);

      if(m_player_video.AddPacket(m_omx_pkt))
        m_omx_pkt = NULL;
      else
        OMXClock::OMXSleep(10);

      if(m_tv_show_info)
      {
        char response[80];
        vc_gencmd(response, sizeof response, "render_bar 4 video_fifo %d %d %d %d", 
                m_player_video.GetDecoderBufferSize()-m_player_video.GetDecoderFreeSpace(),
                0 , 0, m_player_video.GetDecoderBufferSize());
        vc_gencmd(response, sizeof response, "render_bar 5 audio_fifo %d %d %d %d", 
                (int)(100.0*m_player_audio.GetDelay()), 0, 0, 100*AUDIO_BUFFER_SECONDS);
      }
    }
    else if(m_omx_pkt && m_omx_pkt->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      if(m_player_audio.AddPacket(m_omx_pkt))
        m_omx_pkt = NULL;
      else
        OMXClock::OMXSleep(10);
    }
    else if(m_omx_pkt && m_omx_reader.IsActive(OMXSTREAM_SUBTITLE, m_omx_pkt->stream_index))
    {
      if(m_omx_pkt->size && (m_omx_pkt->hints.codec == CODEC_ID_TEXT || 
                             m_omx_pkt->hints.codec == CODEC_ID_SSA))
      {
        if(m_player_video.AddPacket(m_omx_pkt))
          m_omx_pkt = NULL;
        else
          OMXClock::OMXSleep(10);
      }
      else
      {
        m_omx_reader.FreePacket(m_omx_pkt);
        m_omx_pkt = NULL;
      }
    }
    else
    {
      if(m_omx_pkt)
      {
        m_omx_reader.FreePacket(m_omx_pkt);
        m_omx_pkt = NULL;
      }
    }

    /* player got in an error state */
    if(m_player_audio.Error())
    {
      printf("audio player error. emergency exit!!!\n");
      goto do_exit;
    }

    CStdString strSubTitle = m_player_video.GetText();
    if(strSubTitle.length() && m_show_subtitle)
    {
      if(last_sub != strSubTitle)
      {
        last_sub = strSubTitle;
        printf("Text : %s\n", strSubTitle.c_str());
      }
    }

    if(m_stats)
    {
      printf("V : %8.02f %8d %8d A : %8.02f %8.02f Cv : %8d Ca : %8d                            \r",
             m_player_video.GetCurrentPTS() / DVD_TIME_BASE, m_player_video.GetDecoderBufferSize(),
             m_player_video.GetDecoderFreeSpace(), m_player_audio.GetCurrentPTS() / DVD_TIME_BASE, 
             m_player_audio.GetDelay(), m_player_video.GetCached(), m_player_audio.GetCached());
    }
    if(m_omx_reader.IsEof())
        break;

  }

do_exit:
  printf("\n");

  if(!m_stop)
  {
    if(m_has_audio)
      m_player_audio.WaitCompletion();
    else if(m_has_video)
      m_player_video.WaitCompletion();
  }

  m_BcmHost.vc_tv_hdmi_power_on_best(tv_state.width, tv_state.height, tv_state.frame_rate, HDMI_NONINTERLACED,
        (EDID_MODE_MATCH_FLAG_T)(HDMI_MODE_MATCH_FRAMERATE|HDMI_MODE_MATCH_RESOLUTION|HDMI_MODE_MATCH_SCANMODE));

  m_av_clock->OMXStop();
  m_av_clock->OMXStateIdle();

  m_player_video.Close();
  m_player_audio.Close();

  if(m_omx_pkt)
  {
    m_omx_reader.FreePacket(m_omx_pkt);
    m_omx_pkt = NULL;
  }

  m_omx_reader.Close();

  vc_tv_show_info(0);

  g_OMX.Deinitialize();
  g_RBP.Deinitialize();

  printf("have a nice day ;)\n");
  return 1;
}
