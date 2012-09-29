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
#include <string.h>

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
#include "linux/RBP.h"

#include "OMXVideo.h"
#include "OMXAudioCodecOMX.h"
#include "utils/PCMRemap.h"
#include "OMXClock.h"
#include "OMXAudio.h"
#include "OMXReader.h"
#include "OMXPlayerVideo.h"
#include "OMXPlayerAudio.h"
#include "OMXPlayerSubtitles.h"
#include "DllOMX.h"

#include <string>

enum PCMChannels  *m_pChannelMap        = NULL;
volatile sig_atomic_t g_abort           = false;
bool              m_bMpeg               = false;
bool               m_passthrough        = false;
bool              m_Deinterlace         = false;
bool              m_HWDecode            = false;
std::string       deviceString          = "omx:local";
int               m_use_hw_audio        = false;
std::string       m_font_path           = "/usr/share/fonts/truetype/freefont/FreeSans.ttf";
float             m_font_size           = 0.055f;
bool              m_centered            = false;
bool              m_Pause               = false;
OMXReader         m_omx_reader;
int               m_audio_index_use     = -1;
bool              m_buffer_empty        = true;
bool              m_thread_player       = false;
OMXClock          *m_av_clock           = NULL;
COMXStreamInfo    m_hints_audio;
COMXStreamInfo    m_hints_video;
OMXPacket         *m_omx_pkt            = NULL;
bool              m_hdmi_clock_sync     = false;
bool              m_stop                = false;
bool              m_show_subtitle       = false;
int               m_subtitle_index      = 0;
DllBcmHost        m_BcmHost;
OMXPlayerVideo    m_player_video;
OMXPlayerAudio    m_player_audio;
OMXPlayerSubtitles  m_player_subtitles;
int               m_tv_show_info        = 0;
bool              m_has_video           = false;
bool              m_has_audio           = false;
bool              m_has_subtitle        = false;
float             m_display_aspect      = 0.0f;
bool              m_boost_on_downmix    = false;

enum{ERROR=-1,SUCCESS,ONEBYTE};

static struct termios orig_termios;
static void restore_termios()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static int orig_fl;
static void restore_fl()
{
  fcntl(STDIN_FILENO, F_SETFL, orig_fl);
}

void sig_handler(int s)
{
  printf("strg-c catched\n");
  signal(SIGINT, SIG_DFL);
  g_abort = true;
}

void print_usage()
{
  printf("Usage: omxplayer [OPTIONS] [FILE]\n");
  printf("Options :\n");
  printf("         -h / --help                    print this help\n");
//  printf("         -a / --alang language          audio language        : e.g. ger\n");
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
  printf("         -r / --refresh                 adjust framerate/resolution to video\n");
  printf("              --boost-on-downmix        boost volume when downmixing\n");
  printf("              --font path               subtitle font\n");
  printf("                                        (default: /usr/share/fonts/truetype/freefont/FreeSans.ttf)\n");
  printf("              --font-size size          font size as thousandths of screen height\n");
  printf("                                        (default: 55)\n");
  printf("              --align left/center       subtitle alignment (default: left)\n");
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

void FlushStreams(double pts)
{
//  if(m_av_clock)
//    m_av_clock->OMXPause();

  if(m_has_video)
    m_player_video.Flush();

  if(m_has_audio)
    m_player_audio.Flush();

  if(m_has_subtitle)
    m_player_subtitles.Flush();

  if(m_omx_pkt)
  {
    m_omx_reader.FreePacket(m_omx_pkt);
    m_omx_pkt = NULL;
  }

  if(pts != DVD_NOPTS_VALUE)
    m_av_clock->OMXUpdateClock(pts);

//  if(m_av_clock)
//  {
//    m_av_clock->OMXReset();
//    m_av_clock->OMXResume();
//  }
}

void SetVideoMode(int width, int height, int fpsrate, int fpsscale, bool is3d)
{
  int32_t num_modes;
  HDMI_RES_GROUP_T prefer_group;
  int i = 0;
  uint32_t prefer_mode;
  #define TV_MAX_SUPPORTED_MODES 60
  TV_SUPPORTED_MODE_T supported_modes[TV_MAX_SUPPORTED_MODES];
  uint32_t group = HDMI_RES_GROUP_CEA;
  float fps = 60; // better to force to higher rate if no information is known

  if (fpsrate && fpsscale)
    fps = DVD_TIME_BASE / OMXReader::NormalizeFrameduration((double)DVD_TIME_BASE * fpsscale / fpsrate);

  if(is3d)
    group = HDMI_RES_GROUP_CEA_3D;
  else
    group = HDMI_RES_GROUP_CEA;

  num_modes = m_BcmHost.vc_tv_hdmi_get_supported_modes((HDMI_RES_GROUP_T)group,
                                          supported_modes, TV_MAX_SUPPORTED_MODES,
                                          &prefer_group, &prefer_mode);

  TV_SUPPORTED_MODE_T *tv_found = NULL;
  int ifps = (int)(fps+0.5f);
  //printf("num_modes %d, %d, %d\n", num_modes, prefer_group, prefer_mode);

  if (num_modes > 0 && prefer_group != HDMI_RES_GROUP_INVALID)
  {
    TV_SUPPORTED_MODE_T *tv = supported_modes;
    uint32_t best_score = 1<<30;
    uint32_t isNative = tv->native ? 1:0;
    uint32_t w = tv->width;
    uint32_t h = tv->height;
    uint32_t r = tv->frame_rate;
    uint32_t match_flag = HDMI_MODE_MATCH_FRAMERATE | HDMI_MODE_MATCH_RESOLUTION;
    uint32_t scan_mode = 0;
    uint32_t score = 0;

    for (i=0; i<num_modes; i++, tv++)
    {
      isNative = tv->native ? 1:0;
      w = tv->width;
      h = tv->height;
      r = tv->frame_rate;

      //printf("mode %dx%d@%d %s%s:%x\n", tv->width, tv->height, 
      //       tv->frame_rate, tv->native?"N":"", tv->scan_mode?"I":"", tv->code);

      /* Check if frame rate match (equal or exact multiple) */
      if(ifps) 
      {
        if(r == ((r/ifps)*ifps))
          score += abs((int)(r/ifps-1)) * (1<<8); // prefer exact framerate to multiples. Ideal is 1
        else
          score += ((match_flag & HDMI_MODE_MATCH_FRAMERATE) ? (1<<28):(1<<12))/r; // bad - but prefer higher framerate
      }
      /* Check size too, only choose, bigger resolutions */
      if(width && height) 
      {
        /* cost of too small a resolution is high */
        score += max((int)(width - w), 0) * (1<<16);
        score += max((int)(height- h), 0) * (1<<16);
        /* cost of too high a resolution is lower */
        score += max((int)(w-width),   0) * ((match_flag & HDMI_MODE_MATCH_RESOLUTION) ? (1<<8):(1<<0));
        score += max((int)(h-height),  0) * ((match_flag & HDMI_MODE_MATCH_RESOLUTION) ? (1<<8):(1<<0));
      } 
      else if (!isNative) 
      {
        // native is good
        score += 1<<16;
      }

      if (scan_mode != tv->scan_mode) 
        score += (match_flag & HDMI_MODE_MATCH_SCANMODE) ? (1<<20):(1<<8);

      if (w*9 != h*16) // not 16:9 is a small negative
        score += 1<<12;

      /*printf("mode %dx%d@%d %s%s:%x score=%d\n", tv->width, tv->height, 
             tv->frame_rate, tv->native?"N":"", tv->scan_mode?"I":"", tv->code, score);*/

      if (score < best_score) 
      {
        tv_found = tv;
        best_score = score;
      }
      /* reset score */
      score = 0;
    }
  }

  if(tv_found)
  {
    printf("Output mode %d: %dx%d@%d %s%s:%x\n", tv_found->code, tv_found->width, tv_found->height, 
           tv_found->frame_rate, tv_found->native?"N":"", tv_found->scan_mode?"I":"", tv_found->code);
    // if we are closer to ntsc version of framerate, let gpu know
    int ifps = (int)(fps+0.5f);
    bool ntsc_freq = fabs(fps*1001.0f/1000.0f - ifps) < fabs(fps-ifps);
    printf("ntsc_freq:%d\n", ntsc_freq);
    char response[80];
    vc_gencmd(response, sizeof response, "hdmi_ntsc_freqs %d", ntsc_freq);
    m_BcmHost.vc_tv_hdmi_power_on_explicit(HDMI_MODE_HDMI, (HDMI_RES_GROUP_T)group, tv_found->code);
  }
}

int main(int argc, char *argv[])
{
  signal(SIGINT, sig_handler);

  if (isatty(STDIN_FILENO))
  {
    struct termios new_termios;

    tcgetattr(STDIN_FILENO, &orig_termios);

    new_termios             = orig_termios;
    new_termios.c_lflag     &= ~(ICANON | ECHO | ECHOCTL | ECHONL);
    new_termios.c_cflag     |= HUPCL;
    new_termios.c_cc[VMIN]  = 0;


    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    atexit(restore_termios);
  }
  else
  {
    orig_fl = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, orig_fl | O_NONBLOCK);
    atexit(restore_fl);
  }

  std::string last_sub = "";
  std::string            m_filename;
  double                m_incr                = 0;
  CRBP                  g_RBP;
  COMXCore              g_OMX;
  bool                  m_stats               = false;
  bool                  m_dump_format         = false;
  bool                  m_3d                  = false;
  bool                  m_refresh             = false;
  double                startpts              = 0;
  TV_GET_STATE_RESP_T   tv_state;

  const int boost_on_downmix_opt = 0x200;

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
    { "refresh",      no_argument,        NULL,          'r' },
    { "sid",          required_argument,  NULL,          't' },
    { "font",         required_argument,  NULL,          0x100 },
    { "font-size",    required_argument,  NULL,          0x101 },
    { "align",        required_argument,  NULL,          0x102 },
    { "boost-on-downmix", no_argument,    NULL,          boost_on_downmix_opt },
    { 0, 0, 0, 0 }
  };

  int c;
  while ((c = getopt_long(argc, argv, "wihn:o:cslpd3yt:r", longopts, NULL)) != -1)  
  {
    switch (c) 
    {
      case 'r':
        m_refresh = true;
        break;
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
        if(deviceString != "local" && deviceString != "hdmi")
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
      case 0x100:
        m_font_path = optarg;
        break;
      case 0x101:
        {
          const int thousands = atoi(optarg);
          if (thousands > 0)
            m_font_size = thousands*0.001f;
        }
        break;
      case 0x102:
        if (!strcmp(optarg, "center"))
          m_centered = true;
        else
          m_centered = false;
        break;
      case boost_on_downmix_opt:
        m_boost_on_downmix = true;
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
          
  if(m_has_video && m_refresh)
  {
    memset(&tv_state, 0, sizeof(TV_GET_STATE_RESP_T));
    m_BcmHost.vc_tv_get_state(&tv_state);

    if(m_filename.find("3DSBS") != string::npos)
      m_3d = true;

    SetVideoMode(m_hints_video.width, m_hints_video.height, m_hints_video.fpsrate, m_hints_video.fpsscale, m_3d);

  }

  // get display aspect
  TV_GET_STATE_RESP_T current_tv_state;
  memset(&current_tv_state, 0, sizeof(TV_GET_STATE_RESP_T));
  m_BcmHost.vc_tv_get_state(&current_tv_state);

  if(current_tv_state.width && current_tv_state.height)
    m_display_aspect = (float)current_tv_state.width / (float)current_tv_state.height;

  if(m_has_video && !m_player_video.Open(m_hints_video, m_av_clock, m_Deinterlace,  m_bMpeg, 
                                         m_hdmi_clock_sync, m_thread_player, m_display_aspect))
    goto do_exit;

  if(m_has_subtitle &&
     !m_player_subtitles.Open(m_font_path, m_font_size, m_centered, m_av_clock))
    goto do_exit;

  // This is an upper bound check on the subtitle limits. When we pulled the subtitle
  // index from the user we check to make sure that the value is larger than zero, but
  // we couldn't know without scanning the file if it was too high. If this is the case
  // then we replace the subtitle index with the maximum value possible.
  if(m_has_subtitle && m_subtitle_index > (m_omx_reader.SubtitleStreamCount() - 1))
  {
    m_subtitle_index = m_omx_reader.SubtitleStreamCount() - 1;
  }

  // Here we actually enable the subtitle streams if we have one available.
  if (m_show_subtitle && m_has_subtitle && m_subtitle_index <= (m_omx_reader.SubtitleStreamCount() - 1))
    m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, m_subtitle_index);

  m_omx_reader.GetHints(OMXSTREAM_AUDIO, m_hints_audio);

  if(m_has_audio && !m_player_audio.Open(m_hints_audio, m_av_clock, &m_omx_reader, deviceString, 
                                         m_passthrough, m_use_hw_audio,
                                         m_boost_on_downmix, m_thread_player))
    goto do_exit;

  m_av_clock->SetSpeed(DVD_PLAYSPEED_NORMAL);
  m_av_clock->OMXStateExecute();
  m_av_clock->OMXStart();

  struct timespec starttime, endtime;

  printf("Subtitle count : %d state %s : index %d\n", 
      m_omx_reader.SubtitleStreamCount(), m_show_subtitle ? "on" : "off", 
      (m_omx_reader.SubtitleStreamCount() > 0) ? m_subtitle_index + 1 : m_subtitle_index);

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
        if(m_has_audio)
        {
          int new_index = m_omx_reader.GetAudioIndex() - 1;
          if (new_index >= 0)
            m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, new_index);
        }
        break;
      case 'k':
        if(m_has_audio)
          m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, m_omx_reader.GetAudioIndex() + 1);
        break;
      case 'i':
        if(m_omx_reader.GetChapterCount() > 0)
        {
          m_omx_reader.SeekChapter(m_omx_reader.GetChapter() - 1, &startpts);
          FlushStreams(startpts);
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
          FlushStreams(startpts);
        }
        else
        {
          m_incr = 600.0;
        }
        break;
      case 'n':
        if(m_has_subtitle)
        {
          int new_index = m_subtitle_index-1;
          if(new_index >= 0)
          {
            m_subtitle_index = new_index;
            printf("Subtitle count : %d state %s : index %d\n", 
              m_omx_reader.SubtitleStreamCount(), m_show_subtitle ? "on" : "off", 
              (m_omx_reader.SubtitleStreamCount() > 0) ? m_subtitle_index + 1 : m_subtitle_index);
            m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, m_subtitle_index);
            m_player_subtitles.Flush();
          }
        }
        break;
      case 'm':
        if(m_has_subtitle)
        {
          int new_index = m_subtitle_index+1;
          if(new_index < m_omx_reader.SubtitleStreamCount())
          {
            m_subtitle_index = new_index;
            printf("Subtitle count : %d state %s : index %d\n", 
              m_omx_reader.SubtitleStreamCount(), m_show_subtitle ? "on" : "off", 
              (m_omx_reader.SubtitleStreamCount() > 0) ? m_subtitle_index + 1 : m_subtitle_index);
            m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, m_subtitle_index);
            m_player_subtitles.Flush();
          }
        }
        break;
      case 's':
        if(m_has_subtitle)
        {
          if(m_show_subtitle)
          {
            m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, -1);
            m_player_subtitles.Flush();
            m_show_subtitle = false;
          }
          else
          {
            m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, m_subtitle_index);
            m_show_subtitle = true;
          }
          printf("Subtitle count : %d state %s : index %d\n", 
            m_omx_reader.SubtitleStreamCount(), m_show_subtitle ? "on" : "off", 
            (m_omx_reader.SubtitleStreamCount() > 0) ? m_subtitle_index + 1 : m_subtitle_index);
        }
        break;
      case 'q':
        m_stop = true;
        goto do_exit;
        break;
      case 0x5b44: // key left
        if(m_omx_reader.CanSeek()) m_incr = -30.0;
        break;
      case 0x5b43: // key right
        if(m_omx_reader.CanSeek()) m_incr = 30.0;
        break;
      case 0x5b41: // key up
        if(m_omx_reader.CanSeek()) m_incr = 600.0;
        break;
      case 0x5b42: // key down
        if(m_omx_reader.CanSeek()) m_incr = -600.0;
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
      case '-':
        m_player_audio.SetCurrentVolume(m_player_audio.GetCurrentVolume() - 300);
        printf("Current Volume: %.2fdB\n", m_player_audio.GetCurrentVolume() / 100.0f);
        break;
      case '+':
        m_player_audio.SetCurrentVolume(m_player_audio.GetCurrentVolume() + 300);
        printf("Current Volume: %.2fdB\n", m_player_audio.GetCurrentVolume() / 100.0f);
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
      seek_flags = m_incr < 0.0f ? AVSEEK_FLAG_BACKWARD : 0;

      seek_pos *= 1000.0f;

      m_incr = 0;

      if(m_omx_reader.SeekTime(seek_pos, seek_flags, &startpts))
        FlushStreams(startpts);

      m_player_video.Close();
      if(m_has_video && !m_player_video.Open(m_hints_video, m_av_clock, m_Deinterlace,  m_bMpeg, 
                                         m_hdmi_clock_sync, m_thread_player, m_display_aspect))
        goto do_exit;
    }

    /* player got in an error state */
    if(m_player_audio.Error())
    {
      printf("audio player error. emergency exit!!!\n");
      goto do_exit;
    }

    if(m_stats)
    {
      printf("V : %8.02f %8d %8d A : %8.02f %8.02f Cv : %8d Ca : %8d                            \r",
             m_player_video.GetCurrentPTS() / DVD_TIME_BASE, m_player_video.GetDecoderBufferSize(),
             m_player_video.GetDecoderFreeSpace(), m_player_audio.GetCurrentPTS() / DVD_TIME_BASE, 
             m_player_audio.GetDelay(), m_player_video.GetCached(), m_player_audio.GetCached());
    }

    if(m_omx_reader.IsEof() && !m_omx_pkt)
    {
      if (!m_player_audio.GetCached() && !m_player_video.GetCached())
        break;

      // Abort audio buffering, now we're on our own
      if (m_buffer_empty)
        m_av_clock->OMXResume();

      OMXClock::OMXSleep(10);
      continue;
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

    if(m_has_video && m_omx_pkt && m_omx_reader.IsActive(OMXSTREAM_VIDEO, m_omx_pkt->stream_index))
    {
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
    else if(m_has_audio && m_omx_pkt && m_omx_pkt->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      if(m_player_audio.AddPacket(m_omx_pkt))
        m_omx_pkt = NULL;
      else
        OMXClock::OMXSleep(10);
    }
    else if(m_omx_pkt && m_omx_reader.IsActive(OMXSTREAM_SUBTITLE, m_omx_pkt->stream_index))
    {
      if(m_omx_pkt->size && m_show_subtitle &&
          (m_omx_pkt->hints.codec == CODEC_ID_TEXT || 
           m_omx_pkt->hints.codec == CODEC_ID_SSA))
      {
        if(m_player_subtitles.AddPacket(m_omx_pkt))
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
  }

do_exit:
  printf("\n");

  if(!m_stop && !g_abort)
  {
    if(m_has_audio)
      m_player_audio.WaitCompletion();
    else if(m_has_video)
      m_player_video.WaitCompletion();
  }

  if(m_refresh)
  {
    m_BcmHost.vc_tv_hdmi_power_on_best(tv_state.width, tv_state.height, tv_state.frame_rate, HDMI_NONINTERLACED,
                                       (EDID_MODE_MATCH_FLAG_T)(HDMI_MODE_MATCH_FRAMERATE|HDMI_MODE_MATCH_RESOLUTION|HDMI_MODE_MATCH_SCANMODE));
  }

  m_av_clock->OMXStop();
  m_av_clock->OMXStateIdle();

  m_player_subtitles.Close();
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
