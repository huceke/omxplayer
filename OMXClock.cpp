/*
 *      Copyright (C) 2005-2013 Team XBMC
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#elif defined(_WIN32)
#include "system.h"
#endif

#if defined(HAVE_OMXLIB)

//#include "video/VideoReferenceClock.h"
//#include "settings/Settings.h"

#include "OMXClock.h"

OMXClock::OMXClock()
{
  m_dllAvFormat.Load();

  m_has_video   = false;
  m_has_audio   = false;
  m_pause       = false;

  m_omx_speed  = DVD_PLAYSPEED_NORMAL;

  pthread_mutex_init(&m_lock, NULL);

  OMXReset();
}

OMXClock::~OMXClock()
{
  OMXDeinitialize();

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

bool OMXClock::OMXInitialize(bool has_video, bool has_audio)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  const std::string componentName = "OMX.broadcom.clock";

  m_has_video = has_video;
  m_has_audio = has_audio;

  m_pause       = false;

  if(!m_omx_clock.Initialize(componentName, OMX_IndexParamOtherInit))
    return false;

  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);

  if(m_has_audio)
    refClock.eClock = OMX_TIME_RefClockAudio;
  else
    refClock.eClock = OMX_TIME_RefClockVideo;

  omx_err = m_omx_clock.SetConfig(OMX_IndexConfigTimeActiveRefClock, &refClock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Initialize error setting OMX_IndexConfigTimeCurrentAudioReference\n");
    return false;
  }

  return true;
}

void OMXClock::OMXDeinitialize()
{
  if(m_omx_clock.GetComponent() == NULL)
    return;

  m_omx_clock.Deinitialize();
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

  CLog::Log(LOGDEBUG, "OMXClock::OMXStop\n");

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  clock.eState = OMX_TIME_ClockStateStopped;

  omx_err = m_omx_clock.SetConfig(OMX_IndexConfigTimeClockState, &clock);
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

  CLog::Log(LOGDEBUG, "OMXClock::OMXStart(%.0f)", pts);

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  clock.eState = OMX_TIME_ClockStateRunning;
  clock.nStartTime = ToOMXTime((uint64_t)pts);

  omx_err = m_omx_clock.SetConfig(OMX_IndexConfigTimeClockState, &clock);
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

bool OMXClock::OMXStep(int steps /* = 1 */, bool lock /* = true */)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);

  param.nPortIndex = OMX_ALL;
  param.nU32 = steps;

  omx_err = m_omx_clock.SetConfig(OMX_IndexConfigSingleStep, &param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Error setting OMX_IndexConfigSingleStep\n");
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
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(lock)
    Lock();

  OMXStop(false);
  OMXStart(0.0, false);

  if(lock)
    UnLock();

  return true;
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
  //CLog::Log(LOGINFO, "OMXClock::MediaTime %.0f %.0f\n", (double)FromOMXTime(timeStamp.nTimestamp), pts);
  if(lock)
    UnLock();
  
  return pts;
}

double OMXClock::OMXClockAdjustment(bool lock /* = true */)
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

  omx_err = m_omx_clock.GetConfig(OMX_IndexConfigClockAdjustment, &timeStamp);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::MediaTime error getting OMX_IndexConfigClockAdjustment\n");
    if(lock)
      UnLock();
    return 0;
  }

  pts = (double)FromOMXTime(timeStamp.nTimestamp);
  //CLog::Log(LOGINFO, "OMXClock::ClockAdjustment %.0f %.0f\n", (double)FromOMXTime(timeStamp.nTimestamp), pts);
  if(lock)
    UnLock();
  
  return pts;
}


// Set the media time, so calls to get media time use the updated value,
// useful after a seek so mediatime is updated immediately (rather than waiting for first decoded packet)
bool OMXClock::OMXMediaTime(double pts, bool lock /* = true*/)
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

  if(!m_pause)
  {
    if(lock)
      Lock();

    if (OMXSetSpeed(0, false, true))
      m_pause = true;

    if(lock)
      UnLock();
  }
  return m_pause == true;
}

bool OMXClock::OMXResume(bool lock /* = true */)
{
  CLog::Log(LOGINFO, "OMXClock::OMXResume\n");
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(m_pause)
  {
    if(lock)
      Lock();

    if (OMXSetSpeed(m_omx_speed, false, true))
      m_pause = false;

    if(lock)
      UnLock();
  }
  return m_pause == false;
}

#define TRICKPLAY(speed) (speed < 0 || speed > 1.2 * DVD_PLAYSPEED_NORMAL)

bool OMXClock::OMXSetSpeed(int speed, bool lock /* = true */, bool pause_resume /* = false */)
{
  CLog::Log(LOGDEBUG, "OMXClock::OMXSetSpeed(%d)", speed);

  if(m_omx_clock.GetComponent() == NULL)
    return false;
  
  if(lock)
    Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_SCALETYPE scaleType;
  OMX_INIT_STRUCTURE(scaleType);


  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);

  if(m_has_audio && !TRICKPLAY(speed))
    refClock.eClock = OMX_TIME_RefClockAudio;
  else
    refClock.eClock = OMX_TIME_RefClockVideo;

  omx_err = m_omx_clock.SetConfig(OMX_IndexConfigTimeActiveRefClock, &refClock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::OMXSetSpeed error setting OMX_IndexConfigTimeCurrentAudioReference\n");
    return false;
  }
  if (TRICKPLAY(speed))
    OMXStep(-1, false);
  else
    OMXStep(0, false);

  if (0 && TRICKPLAY(speed))
  {
    scaleType.xScale = 0;
  }
  else
  {
    scaleType.xScale = (speed << 16) / DVD_PLAYSPEED_NORMAL;
  }
  omx_err = m_omx_clock.SetConfig(OMX_IndexConfigTimeScale, &scaleType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::OMXSetSpeed error setting OMX_IndexConfigTimeClockState\n");
    if(lock)
      UnLock();
    return false;
  }

  if (!pause_resume)
    m_omx_speed = speed;

  if(lock)
    UnLock();

  return true;
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

  omx_err = m_omx_clock.SetConfig(OMX_IndexConfigLatencyTarget, &latencyTarget);
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

void OMXClock::OMXSleep(unsigned int dwMilliSeconds)
{
  struct timespec req;
  req.tv_sec = dwMilliSeconds / 1000;
  req.tv_nsec = (dwMilliSeconds % 1000) * 1000000;

  while ( nanosleep(&req, &req) == -1 && errno == EINTR && (req.tv_nsec > 0 || req.tv_sec > 0));
}

static int64_t CurrentHostCounter(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return( ((int64_t)now.tv_sec * 1000000000L) + now.tv_nsec );
}

static int64_t CurrentHostFrequency(void)
{
  return( (int64_t)1000000000L );
}

int64_t OMXClock::GetAbsoluteClock()
{
  return CurrentHostCounter();
}

double OMXClock::GetClock(bool interpolated /*= true*/)
{
  return CurrentHostCounter();
  //return OMXMediaTime();
}
#endif

