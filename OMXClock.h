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

#ifndef _AVCLOCK_H_
#define _AVCLOCK_H_

#include "DllAvFormat.h"

#include "OMXCore.h"

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0
#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20

#define DVD_TIME_BASE 1000000
#define DVD_NOPTS_VALUE    (-1LL<<52) // should be possible to represent in both double and __int64

#define DVD_TIME_TO_SEC(x)  ((int)((double)(x) / DVD_TIME_BASE))
#define DVD_TIME_TO_MSEC(x) ((int)((double)(x) * 1000 / DVD_TIME_BASE))
#define DVD_SEC_TO_TIME(x)  ((double)(x) * DVD_TIME_BASE)
#define DVD_MSEC_TO_TIME(x) ((double)(x) * DVD_TIME_BASE / 1000)

#define DVD_PLAYSPEED_PAUSE       0       // frame stepping
#define DVD_PLAYSPEED_NORMAL      1000

#ifdef OMX_SKIP64BIT
static inline OMX_TICKS ToOMXTime(int64_t pts)
{
  OMX_TICKS ticks;
  ticks.nLowPart = pts;
  ticks.nHighPart = pts >> 32;
  return ticks;
}
static inline uint64_t FromOMXTime(OMX_TICKS ticks)
{
  uint64_t pts = ticks.nLowPart | ((uint64_t)ticks.nHighPart << 32);
  return pts;
}
#else
#define FromOMXTime(x) (x)
#define ToOMXTime(x) (x)
#endif

enum {
  AV_SYNC_AUDIO_MASTER,
  AV_SYNC_VIDEO_MASTER,
  AV_SYNC_EXTERNAL_MASTER,
};

class OMXClock
{
protected:
  double            m_video_clock;
  double            m_audio_clock;
  bool              m_pause;
  double            m_iCurrentPts;
  bool              m_has_video;
  bool              m_has_audio;
  int               m_play_speed;
  pthread_mutex_t   m_lock;
  void              CheckSystemClock();
  double            SystemToAbsolute(int64_t system);
  double            SystemToPlaying(int64_t system);
  int64_t           m_systemUsed;
  int64_t           m_startClock;
  int64_t           m_pauseClock;
  double            m_iDisc;
  bool              m_bReset;
  static int64_t    m_systemFrequency;
  static int64_t    m_systemOffset;
  int64_t           m_ClockOffset;
  double            m_maxspeedadjust;
  bool              m_speedadjust;
  static bool       m_ismasterclock;
  double            m_fps;
private:
  COMXCoreComponent m_omx_clock;
  DllAvFormat       m_dllAvFormat;
public:
  OMXClock();
  ~OMXClock();
  void Lock();
  void UnLock();
  int64_t GetFrequency();
  int64_t GetTime(bool interpolated = true);
  double  GetAbsoluteClock(bool interpolated = true);
  int64_t Wait(int64_t Target);
  double  WaitAbsoluteClock(double target);
  double GetClock(bool interpolated = true);
  double GetClock(double& absolute, bool interpolated = true);
  void SetSpeed(int iSpeed);
  void SetMasterClock(bool ismasterclock) { m_ismasterclock = ismasterclock; }
  bool IsMasterClock()                    { return m_ismasterclock;          }
  void Discontinuity(double currentPts = 0LL);

  void Reset() { m_bReset = true; }
  void Pause();
  void Resume();

  int UpdateFramerate(double fps, double* interval = NULL);
  bool   SetMaxSpeedAdjust(double speed);

  bool OMXInitialize(bool has_video, bool has_audio);
  void Deinitialize();
  bool OMXIsPaused() { return m_pause; };
  bool OMXStop(bool lock = true);
  bool OMXStart(bool lock = true);
  bool OMXReset(bool lock = true);
  double OMXWallTime(bool lock = true);
  double OMXMediaTime(bool lock = true);
  bool OMXPause(bool lock = true);
  bool OMXResume(bool lock = true);
  bool OMXUpdateClock(double pts, bool lock = true);
  bool OMXWaitStart(double pts, bool lock = true);
  bool OMXSpeed(int speed, bool lock = true);
  int  OMXPlaySpeed() { return m_play_speed; };
  COMXCoreComponent *GetOMXClock();
  bool OMXStatePause(bool lock = true);
  bool OMXStateExecute(bool lock = true);
  void OMXStateIdle(bool lock = true);
  double GetPTS();
  void   SetPTS(double pts);
  static void AddTimespecs(struct timespec &time, long millisecs);
  bool HDMIClockSync(bool lock = true);
  static int64_t CurrentHostCounter(void);
  static int64_t CurrentHostFrequency(void);
  void  SetVideoClock(double video_clock) { m_video_clock = video_clock; };
  void  SetAudioClock(double audio_clock) { m_audio_clock = audio_clock; };
  double  GetVideoClock() { return m_video_clock; };
  double  GetAudioClock() { return m_audio_clock; };
  bool HasVideo() { return m_has_video; };
  bool HasAudio() { return m_has_audio; };
  static void AddTimeSpecNano(struct timespec &time, uint64_t nanoseconds);
  static void OMXSleep(unsigned int dwMilliSeconds);

  int     GetRefreshRate(double* interval = NULL);
  void    SetRefreshRate(double fps) { m_fps = fps; };
};

#endif
