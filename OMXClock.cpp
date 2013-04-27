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

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#elif defined(_WIN32)
#include "system.h"
#endif

#include "OMXClock.h"

#define OMX_PRE_ROLL 0//200

int64_t OMXClock::m_systemOffset;
int64_t OMXClock::m_systemFrequency;
bool    OMXClock::m_ismasterclock;

OMXClock::OMXClock()
{
  m_dllAvFormat.Load();

  m_has_video   = false;
  m_has_audio   = false;
  m_play_speed  = DVD_PLAYSPEED_NORMAL;
  m_pause       = false;

  m_systemFrequency = CurrentHostFrequency();
  m_systemUsed = m_systemFrequency;
  m_pauseClock = 0;
  m_bReset = true;
  m_ismasterclock = true;
  m_ClockOffset = 0;

  pthread_mutex_init(&m_lock, NULL);

  CheckSystemClock();

  OMXReset();
}

OMXClock::~OMXClock()
{
  Deinitialize();

  m_dllAvFormat.Unload();
  pthread_mutex_destroy(&m_lock);
}

void OMXClock::Lock()
{
  pthread_mutex_lock(&m_lock);
}

void OMXClock::UnLock()
{
  pthread_mutex_unlock(&m_lock);
}

double OMXClock::SystemToAbsolute(int64_t system)
{
  return DVD_TIME_BASE * (double)(system - m_systemOffset) / m_systemFrequency;
}

double OMXClock::SystemToPlaying(int64_t system)
{
  int64_t current;

  if (m_bReset)
  {
    m_startClock = system;
    m_systemUsed = m_systemFrequency;
    m_pauseClock = 0;
    m_bReset = false;
  }

  if (m_pauseClock)
    current = m_pauseClock;
  else
    current = system;

  return DVD_TIME_BASE * (double)(current - m_startClock) / m_systemUsed;
}

int64_t OMXClock::GetFrequency()
{
  return m_systemFrequency;
}

int64_t OMXClock::Wait(int64_t Target)
{
  int64_t       Now;
  int           SleepTime;
  int64_t       ClockOffset = m_ClockOffset;

  Now = CurrentHostCounter();
  //sleep until the timestamp has passed
  SleepTime = (int)((Target - (Now + ClockOffset)) * 1000 / m_systemFrequency);
  if (SleepTime > 0)
    OMXSleep(SleepTime);

  Now = CurrentHostCounter();
  return Now;
}

double OMXClock::WaitAbsoluteClock(double target)
{
  Lock();
  int64_t systemtarget, freq, offset;
  freq   = m_systemFrequency;
  offset = m_systemOffset;
  UnLock();

  systemtarget = (int64_t)(target / DVD_TIME_BASE * (double)freq);
  systemtarget += offset;
  systemtarget = Wait(systemtarget);
  systemtarget -= offset;
  return (double)systemtarget / freq * DVD_TIME_BASE;
}

// Returns the current absolute clock in units of DVD_TIME_BASE (usually microseconds).
double OMXClock::GetAbsoluteClock(bool interpolated /*= true*/)
{
  Lock();
  CheckSystemClock();
  double current = GetTime();
  UnLock();
  return SystemToAbsolute(current);
}

int64_t OMXClock::GetTime(bool interpolated)
{
  return CurrentHostCounter() + m_ClockOffset;
}

void OMXClock::CheckSystemClock()
{
  if(!m_systemFrequency)
    m_systemFrequency = GetFrequency();

  if(!m_systemOffset)
    m_systemOffset = GetTime();
}

double OMXClock::GetClock(bool interpolated /*= true*/)
{
  Lock();
  double clock = GetTime(interpolated);
  UnLock();
  return SystemToPlaying(clock);
}

double OMXClock::GetClock(double& absolute, bool interpolated /*= true*/)
{
  int64_t current = GetTime(interpolated);

  Lock();
  CheckSystemClock();
  absolute = SystemToAbsolute(current);
  UnLock();

  return SystemToPlaying(current);
}

void OMXClock::SetSpeed(int iSpeed)
{
  // this will sometimes be a little bit of due to rounding errors, ie clock might jump abit when changing speed
  Lock();

  if(iSpeed == DVD_PLAYSPEED_PAUSE)
  {
    if(!m_pauseClock)
      m_pauseClock = GetTime();
    UnLock();
    return;
  }

  int64_t current;
  int64_t newfreq = m_systemFrequency * DVD_PLAYSPEED_NORMAL / iSpeed;

  current = GetTime();
  if( m_pauseClock )
  {
    m_startClock += current - m_pauseClock;
    m_pauseClock = 0;
  }

  m_startClock = current - (int64_t)((double)(current - m_startClock) * newfreq / m_systemUsed);
  m_systemUsed = newfreq;
  UnLock();
}

void OMXClock::Pause()
{
  CLog::Log(LOGINFO, "OMXClock::Pause\n");
  Lock();
  if(!m_pauseClock)
    m_pauseClock = GetTime();
  UnLock();
}

void OMXClock::Resume()
{
  CLog::Log(LOGINFO, "OMXClock::Resume\n");
  Lock();
  if( m_pauseClock )
  {
    int64_t current;
    current = GetTime();

    m_startClock += current - m_pauseClock;
    m_pauseClock = 0;
  }
  UnLock();
}

bool OMXClock::OMXInitialize(bool has_video, bool has_audio)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  std::string componentName = "";

  m_has_video = has_video;
  m_has_audio = has_audio;

  componentName = "OMX.broadcom.clock";
  if(!m_omx_clock.Initialize(componentName, OMX_IndexParamOtherInit))
    return false;

#if 0
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  clock.eState = OMX_TIME_ClockStateWaitingForStartTime;
  clock.nOffset   = ToOMXTime(-1000LL * OMX_PRE_ROLL);

  if(m_has_audio)
  {
    clock.nWaitMask |= OMX_CLOCKPORT0;
  }
  if(m_has_video)
  {
    clock.nWaitMask |= OMX_CLOCKPORT1;
    clock.nWaitMask |= OMX_CLOCKPORT2;
  }

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Initialize error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }
#endif
  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);

  if(m_has_audio)
    refClock.eClock = OMX_TIME_RefClockAudio;
  else
    refClock.eClock = OMX_TIME_RefClockVideo;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeActiveRefClock, &refClock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Initialize error setting OMX_IndexConfigTimeCurrentAudioReference\n");
    return false;
  }

  return true;
}

void OMXClock::Deinitialize()
{
  if(m_omx_clock.GetComponent() == NULL)
    return;

  m_omx_clock.Deinitialize();
}

bool OMXClock::OMXStatePause(bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  if(m_omx_clock.GetState() != OMX_StatePause)
  {
    OMX_ERRORTYPE omx_err = OMX_ErrorNone;
    omx_err = m_omx_clock.SetStateForComponent(OMX_StatePause);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::StatePause m_omx_clock.SetStateForComponent\n");
      if(lock)
        UnLock();
      return false;
    }
  }

  if(lock)
    UnLock();

  return true;
}

bool OMXClock::OMXStateExecute(bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  if(m_omx_clock.GetState() != OMX_StateExecuting)
  {
    OMX_ERRORTYPE omx_err = OMX_ErrorNone;
    omx_err = m_omx_clock.SetStateForComponent(OMX_StateExecuting);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::StateExecute m_omx_clock.SetStateForComponent\n");
      if(lock)
        UnLock();
      return false;
    }
  }

  if(lock)
    UnLock();

  return true;
}

void OMXClock::OMXStateIdle(bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return;

  if(lock)
    Lock();

  if(m_omx_clock.GetState() == OMX_StateExecuting)
    m_omx_clock.SetStateForComponent(OMX_StatePause);

  if(m_omx_clock.GetState() != OMX_StateIdle)
    m_omx_clock.SetStateForComponent(OMX_StateIdle);

  if(lock)
    UnLock();
}

COMXCoreComponent *OMXClock::GetOMXClock()
{
  if(!m_omx_clock.GetComponent())
    return NULL;

  return &m_omx_clock;
}

bool  OMXClock::OMXStop(bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  clock.eState = OMX_TIME_ClockStateStopped;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Stop error setting OMX_IndexConfigTimeClockState\n");
    if(lock)
      UnLock();
    return false;
  }

  if(lock)
    UnLock();

  return true;
}

bool OMXClock::OMXStart(double pts, bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  clock.eState = OMX_TIME_ClockStateRunning;
  clock.nStartTime = ToOMXTime((uint64_t)pts);

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Start error setting OMX_IndexConfigTimeClockState\n");
    if(lock)
      UnLock();
    return false;
  }

  if(lock)
    UnLock();

  return true;
}

bool OMXClock::OMXReset(bool lock /* = true */)
{
  if(lock)
    Lock();

  if(m_omx_clock.GetComponent() != NULL)
  {
    OMX_ERRORTYPE omx_err = OMX_ErrorNone;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
    OMX_INIT_STRUCTURE(clock);

    OMXStop(false);

    clock.eState    = OMX_TIME_ClockStateWaitingForStartTime;
    clock.nOffset   = ToOMXTime(-1000LL * OMX_PRE_ROLL);

    if(m_has_audio)
    {
      clock.nWaitMask |= OMX_CLOCKPORT0;
    }
    if(m_has_video)
    {
      clock.nWaitMask |= OMX_CLOCKPORT1;
      clock.nWaitMask |= OMX_CLOCKPORT2;
    }

    omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::Reset error setting OMX_IndexConfigTimeClockState\n");
    if(lock)
      UnLock();
      return false;
    }

    OMXStart(0.0, false);
  }

  if(lock)
    UnLock();

  return true;
}

double OMXClock::OMXWallTime(bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return 0;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  double pts = 0;

  OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
  OMX_INIT_STRUCTURE(timeStamp);
  timeStamp.nPortIndex = m_omx_clock.GetInputPort();

  omx_err = m_omx_clock.GetConfig(OMX_IndexConfigTimeCurrentWallTime, &timeStamp);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::WallTime error getting OMX_IndexConfigTimeCurrentWallTime\n");
    if(lock)
      UnLock();
    return 0;
  }

  pts = FromOMXTime(timeStamp.nTimestamp);

  if(lock)
    UnLock();
  
  return pts;
}

double OMXClock::OMXMediaTime(bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return 0;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  double pts = 0;

  OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
  OMX_INIT_STRUCTURE(timeStamp);
  timeStamp.nPortIndex = m_omx_clock.GetInputPort();

  omx_err = m_omx_clock.GetConfig(OMX_IndexConfigTimeCurrentMediaTime, &timeStamp);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::MediaTime error getting OMX_IndexConfigTimeCurrentMediaTime\n");
    if(lock)
      UnLock();
    return 0;
  }

  pts = (double)FromOMXTime(timeStamp.nTimestamp);
  CLog::Log(LOGINFO, "OMXClock::MediaTime %.0f %.0f\n", (double)FromOMXTime(timeStamp.nTimestamp), pts);
  if(lock)
    UnLock();
  
  return pts;
}

// Set the media time, so calls to get media time use the updated value,
// useful after a seek so mediatime is updated immediately (rather than waiting for first decoded packet)
bool OMXClock::OMXMediaTime(double pts, bool fixPreroll /* = true*/, bool lock /* = true*/)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_INDEXTYPE index;
  OMX_TIME_CONFIG_TIMESTAMPTYPE timeStamp;
  OMX_INIT_STRUCTURE(timeStamp);
  timeStamp.nPortIndex = m_omx_clock.GetInputPort();

  if(m_has_audio)
    index = OMX_IndexConfigTimeCurrentAudioReference;
  else
    index = OMX_IndexConfigTimeCurrentVideoReference;

  if(fixPreroll)
    pts -= (OMX_PRE_ROLL * 1000);
  timeStamp.nTimestamp = ToOMXTime(pts);

  omx_err = m_omx_clock.SetConfig(index, &timeStamp);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::OMXMediaTime error setting %s", index == OMX_IndexConfigTimeCurrentAudioReference ?
       "OMX_IndexConfigTimeCurrentAudioReference":"OMX_IndexConfigTimeCurrentVideoReference");
    if(lock)
      UnLock();
    return false;
  }

  CLog::Log(LOGDEBUG, "OMXClock::OMXMediaTime set config %s = %.2f (%.2f)", index == OMX_IndexConfigTimeCurrentAudioReference ?
       "OMX_IndexConfigTimeCurrentAudioReference":"OMX_IndexConfigTimeCurrentVideoReference", pts, OMXMediaTime(false));

  if(lock)
    UnLock();

  return true;
}

bool OMXClock::OMXPause(bool lock /* = true */)
{
  CLog::Log(LOGINFO, "OMXClock::OMXPause\n");
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(m_pause)
    return true;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_SCALETYPE scaleType;
  OMX_INIT_STRUCTURE(scaleType);

  scaleType.xScale = 0; // pause

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeScale, &scaleType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Pause error setting OMX_IndexConfigTimeClockState\n");
    if(lock)
      UnLock();
    return false;
  }

  m_pause = true;

  if(lock)
    UnLock();

  return true;
}

bool OMXClock::OMXResume(bool lock /* = true */)
{
  CLog::Log(LOGINFO, "OMXClock::OMXResume\n");
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(!m_pause)
    return true;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_SCALETYPE scaleType;
  OMX_INIT_STRUCTURE(scaleType);

  scaleType.xScale = (m_play_speed << 16) / DVD_PLAYSPEED_NORMAL;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeScale, &scaleType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Resume error setting OMX_IndexConfigTimeClockState\n");
    if(lock)
      UnLock();
    return false;
  }

  m_pause = false;

  if(lock)
    UnLock();

  return true;
}

bool OMXClock::OMXUpdateClock(double pts, bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_TIMESTAMPTYPE ts;
  OMX_INIT_STRUCTURE(ts);

  ts.nPortIndex = OMX_ALL;
  ts.nTimestamp = ToOMXTime((uint64_t)pts);

  if(m_has_audio)
  {
    omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeCurrentAudioReference, &ts);
    if(omx_err != OMX_ErrorNone)
      CLog::Log(LOGERROR, "OMXClock::OMXUpdateClock error setting OMX_IndexConfigTimeCurrentAudioReference\n");
  }
  else
  {
    omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeCurrentVideoReference, &ts);
    if(omx_err != OMX_ErrorNone)
      CLog::Log(LOGERROR, "OMXClock::OMXUpdateClock error setting OMX_IndexConfigTimeCurrentVideoReference\n");
  }

  if(lock)
    UnLock();

  return true;
}

bool OMXClock::OMXWaitStart(double pts, bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  if(pts == DVD_NOPTS_VALUE)
    pts = 0;

  clock.nStartTime = ToOMXTime((uint64_t)pts);

  if(pts == DVD_NOPTS_VALUE)
  {
    clock.eState = OMX_TIME_ClockStateRunning;
    clock.nWaitMask = 0;
  }
  else
  {
    clock.eState = OMX_TIME_ClockStateWaitingForStartTime;
    clock.nOffset   = ToOMXTime(-1000LL * OMX_PRE_ROLL);

    if(m_has_audio)
    {
      clock.nWaitMask |= OMX_CLOCKPORT0;
    }
    if(m_has_video)
    {
      clock.nWaitMask |= OMX_CLOCKPORT1;
      clock.nWaitMask |= OMX_CLOCKPORT2;
    }
  }

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::OMXWaitStart error setting OMX_IndexConfigTimeClockState\n");
    if(lock)
      UnLock();
    return false;
  }

  if(lock)
    UnLock();

  return true;
}

bool OMXClock::OMXSpeed(int speed, bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;
  
  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_SCALETYPE scaleType;
  OMX_INIT_STRUCTURE(scaleType);

  m_play_speed = speed;

  scaleType.xScale = (m_play_speed << 16) / DVD_PLAYSPEED_NORMAL;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeScale, &scaleType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Speed error setting OMX_IndexConfigTimeClockState\n");
    if(lock)
      UnLock();
    return false;
  }

  if(lock)
    UnLock();

  return true;
}

void OMXClock::AddTimespecs(struct timespec &time, long millisecs)
{
   time.tv_sec  += millisecs / 1000;
   time.tv_nsec += (millisecs % 1000) * 1000000;
   if (time.tv_nsec > 1000000000)
   {
      time.tv_sec  += 1;
      time.tv_nsec -= 1000000000;
   }
}

bool OMXClock::HDMIClockSync(bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
  OMX_INIT_STRUCTURE(latencyTarget);

  latencyTarget.nPortIndex = OMX_ALL;
  latencyTarget.bEnabled = OMX_TRUE;
  latencyTarget.nFilter = 10;
  latencyTarget.nTarget = 0;
  latencyTarget.nShift = 3;
  latencyTarget.nSpeedFactor = -200;
  latencyTarget.nInterFactor = 100;
  latencyTarget.nAdjCap = 100;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigLatencyTarget, &latencyTarget);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Speed error setting OMX_IndexConfigLatencyTarget\n");
    if(lock)
      UnLock();
    return false;
  }

  if(lock)
    UnLock();

  return true;
}

int64_t OMXClock::CurrentHostCounter(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return( ((int64_t)now.tv_sec * 1000000000L) + now.tv_nsec );
}

int64_t OMXClock::CurrentHostFrequency(void)
{
  return( (int64_t)1000000000L );
}

void OMXClock::AddTimeSpecNano(struct timespec &time, uint64_t nanoseconds)
{
   time.tv_sec  += nanoseconds / 1000000000;
   time.tv_nsec += (nanoseconds % 1000000000);
   if (time.tv_nsec > 1000000000)
   {
      time.tv_sec  += 1;
      time.tv_nsec -= 1000000000;
   }
}

void OMXClock::OMXSleep(unsigned int dwMilliSeconds)
{
  struct timespec req;
  req.tv_sec = dwMilliSeconds / 1000;
  req.tv_nsec = (dwMilliSeconds % 1000) * 1000000;

  while ( nanosleep(&req, &req) == -1 && errno == EINTR && (req.tv_nsec > 0 || req.tv_sec > 0));
}

