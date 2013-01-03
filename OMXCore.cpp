/*
 *      Copyright (C) 2010-2012 Team XBMCn
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

#include <math.h>
#include <sys/time.h>

#if defined(HAVE_OMXLIB)
#include "OMXCore.h"
#include "utils/log.h"

#include "OMXClock.h"

#ifdef _LINUX
#include "XMemUtils.h"
#endif

//#define OMX_DEBUG_EVENTS
//#define OMX_DEBUG_EVENTHANDLER

////////////////////////////////////////////////////////////////////////////////////////////
#define CLASSNAME "COMXCoreComponent"
////////////////////////////////////////////////////////////////////////////////////////////

static void add_timespecs(struct timespec &time, long millisecs)
{
   time.tv_sec  += millisecs / 1000;
   time.tv_nsec += (millisecs % 1000) * 1000000;
   if (time.tv_nsec > 1000000000)
   {
      time.tv_sec  += 1;
      time.tv_nsec -= 1000000000;
   }
}


COMXCoreTunel::COMXCoreTunel()
{
  m_src_component       = NULL;
  m_dst_component       = NULL;
  m_src_port            = 0;
  m_dst_port            = 0;
  m_portSettingsChanged = false;
  m_DllOMX              = new DllOMX();
  m_DllOMXOpen          = m_DllOMX->Load();

  pthread_mutex_init(&m_lock, NULL);
}

COMXCoreTunel::~COMXCoreTunel()
{
  Deestablish();
  if(m_DllOMXOpen)
    m_DllOMX->Unload();
  delete m_DllOMX;

  pthread_mutex_destroy(&m_lock);
}

void COMXCoreTunel::Lock()
{
  pthread_mutex_lock(&m_lock);
}

void COMXCoreTunel::UnLock()
{
  pthread_mutex_unlock(&m_lock);
}

void COMXCoreTunel::Initialize(COMXCoreComponent *src_component, unsigned int src_port, COMXCoreComponent *dst_component, unsigned int dst_port)
{
  if(!m_DllOMXOpen)
    return;
  m_src_component  = src_component;
  m_src_port    = src_port;
  m_dst_component  = dst_component;
  m_dst_port    = dst_port;
}

OMX_ERRORTYPE COMXCoreTunel::Flush()
{
  if(!m_DllOMXOpen)
    return OMX_ErrorUndefined;

  if(!m_src_component || !m_dst_component)
    return OMX_ErrorUndefined;

  Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  if(m_src_component->GetComponent())
  {
    omx_err = OMX_SendCommand(m_src_component->GetComponent(), OMX_CommandFlush, m_src_port, NULL);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Flush - Error flush  port %d on component %s omx_err(0x%08x)", 
          m_src_port, m_src_component->GetName().c_str(), (int)omx_err);
    }
  }

  if(m_dst_component->GetComponent())
  {
    omx_err = OMX_SendCommand(m_dst_component->GetComponent(), OMX_CommandFlush, m_dst_port, NULL);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Flush - Error flush port %d on component %s omx_err(0x%08x)", 
          m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
    }
  }

  if(m_src_component->GetComponent())
    omx_err = m_src_component->WaitForCommand(OMX_CommandFlush, m_src_port);

  if(m_dst_component->GetComponent())
    omx_err = m_dst_component->WaitForCommand(OMX_CommandFlush, m_dst_port);

  UnLock();

  return OMX_ErrorNone;
}

OMX_ERRORTYPE COMXCoreTunel::Deestablish(bool noWait)
{
  if(!m_DllOMXOpen)
    return OMX_ErrorUndefined;

  if(!m_src_component || !m_dst_component)
    return OMX_ErrorUndefined;

  Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(m_src_component->GetComponent() && m_portSettingsChanged && !noWait)
    omx_err = m_src_component->WaitForEvent(OMX_EventPortSettingsChanged);

  if(m_src_component->GetComponent())
  {
    omx_err = m_src_component->DisablePort(m_src_port, false);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Deestablish - Error disable port %d on component %s omx_err(0x%08x)", 
          m_src_port, m_src_component->GetName().c_str(), (int)omx_err);
    }
  }

  if(m_dst_component->GetComponent())
  {
    omx_err = m_dst_component->DisablePort(m_dst_port, false);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState) 
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Deestablish - Error disable port %d on component %s omx_err(0x%08x)", 
          m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
    }
  }

  if(m_src_component->GetComponent())
  {
    omx_err = m_DllOMX->OMX_SetupTunnel(m_src_component->GetComponent(), m_src_port, NULL, 0);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorIncorrectStateOperation) 
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Deestablish - could not unset tunnel on comp src %s port %d omx_err(0x%08x)\n", 
          m_src_component->GetName().c_str(), m_src_port, (int)omx_err);
    }
  }

  if(m_dst_component->GetComponent())
  {
    omx_err = m_DllOMX->OMX_SetupTunnel(m_dst_component->GetComponent(), m_dst_port, NULL, 0);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorIncorrectStateOperation) 
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Deestablish - could not unset tunnel on comp dst %s port %d omx_err(0x%08x)\n", 
          m_dst_component->GetName().c_str(), m_dst_port, (int)omx_err);
    }
  }

  UnLock();

  return OMX_ErrorNone;
}

OMX_ERRORTYPE COMXCoreTunel::Establish(bool portSettingsChanged)
{
  if(!m_DllOMXOpen)
    return OMX_ErrorUndefined;

  Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_PARAM_U32TYPE param;
  OMX_INIT_STRUCTURE(param);

  if(!m_src_component || !m_dst_component)
  {
    UnLock();
    return OMX_ErrorUndefined;
  }

  if(m_src_component->GetState() == OMX_StateLoaded)
  {
    omx_err = m_src_component->SetStateForComponent(OMX_StateIdle);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Establish - Error setting state to idle %s omx_err(0x%08x)", 
          m_src_component->GetName().c_str(), (int)omx_err);
      UnLock();
      return omx_err;
    }
  }

  if(portSettingsChanged)
  {
    omx_err = m_src_component->WaitForEvent(OMX_EventPortSettingsChanged);
    if(omx_err != OMX_ErrorNone)
    {
      UnLock();
      return omx_err;
    }
  }

  if(m_src_component->GetComponent())
  {
    omx_err = m_src_component->DisablePort(m_src_port, false);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState) 
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Establish - Error disable port %d on component %s omx_err(0x%08x)",
          m_src_port, m_src_component->GetName().c_str(), (int)omx_err);
    }
  }

  if(m_dst_component->GetComponent())
  {
    omx_err = m_dst_component->DisablePort(m_dst_port, false);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState) {
      CLog::Log(LOGERROR, "COMXCoreComponent::Establish - Error disable port %d on component %s omx_err(0x%08x)",
          m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
    }
  }

  if(m_src_component->GetComponent() && m_dst_component->GetComponent())
  {
    omx_err = m_DllOMX->OMX_SetupTunnel(m_src_component->GetComponent(), m_src_port, m_dst_component->GetComponent(), m_dst_port);
    if(omx_err != OMX_ErrorNone) 
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Establish - could not setup tunnel src %s port %d dst %s port %d omx_err(0x%08x)\n", 
          m_src_component->GetName().c_str(), m_src_port, m_dst_component->GetName().c_str(), m_dst_port, (int)omx_err);
      UnLock();
      return omx_err;
    }
  }
  else
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::Establish - could not setup tunnel\n");
    UnLock();
    return OMX_ErrorUndefined;
  }

  if(m_src_component->GetComponent())
  {
    omx_err = m_src_component->EnablePort(m_src_port, false);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Establish - Error enable port %d on component %s omx_err(0x%08x)", 
          m_src_port, m_src_component->GetName().c_str(), (int)omx_err);
      UnLock();
      return omx_err;
    }
  }

  if(m_dst_component->GetComponent())
  {
    omx_err = m_dst_component->EnablePort(m_dst_port, false);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Establish - Error enable port %d on component %s omx_err(0x%08x)", 
          m_dst_port, m_dst_component->GetName().c_str(), (int)omx_err);
      UnLock();
      return omx_err;
    }
  }

  if(m_dst_component->GetComponent())
  {
    if(m_dst_component->GetState() == OMX_StateLoaded)
    {
      omx_err = m_dst_component->WaitForCommand(OMX_CommandPortEnable, m_dst_port);
      if(omx_err != OMX_ErrorNone)
      {
        UnLock();
        return omx_err;
      }
    
      omx_err = m_dst_component->SetStateForComponent(OMX_StateIdle);
      if(omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "COMXCoreComponent::Establish - Error setting state to idle %s omx_err(0x%08x)", 
            m_src_component->GetName().c_str(), (int)omx_err);
        UnLock();
        return omx_err;
      }
    }
    else
    {
      omx_err = m_dst_component->WaitForCommand(OMX_CommandPortEnable, m_dst_port);
      if(omx_err != OMX_ErrorNone)
      {
        UnLock();
        return omx_err;
      }
    }
  }

  if(m_src_component->GetComponent())
  {
    omx_err = m_src_component->WaitForCommand(OMX_CommandPortEnable, m_src_port);
    if(omx_err != OMX_ErrorNone)
    {
      UnLock();
      return omx_err;
    }
  }

  m_portSettingsChanged = portSettingsChanged;

  UnLock();

  return OMX_ErrorNone;
}

////////////////////////////////////////////////////////////////////////////////////////////

COMXCoreComponent::COMXCoreComponent()
{
  m_input_port  = 0;
  m_output_port = 0;
  m_handle      = NULL;

  m_input_alignment     = 0;
  m_input_buffer_size  = 0;
  m_input_buffer_count  = 0;

  m_output_alignment    = 0;
  m_output_buffer_size  = 0;
  m_output_buffer_count = 0;
  m_flush_input         = false;
  m_flush_output        = false;

  CustomDecoderFillBufferDoneHandler = NULL;
  CustomDecoderEmptyBufferDoneHandler = NULL;

  m_eos                 = false;

  m_exit = false;
  m_DllOMXOpen = false;

  pthread_mutex_init(&m_omx_input_mutex, NULL);
  pthread_mutex_init(&m_omx_output_mutex, NULL);
  pthread_mutex_init(&m_omx_event_mutex, NULL);
  pthread_cond_init(&m_input_buffer_cond, NULL);
  pthread_cond_init(&m_output_buffer_cond, NULL);
  pthread_cond_init(&m_omx_event_cond, NULL);

  m_omx_input_use_buffers  = false;
  m_omx_output_use_buffers = false;

  m_DllOMX = new DllOMX();

  pthread_mutex_init(&m_lock, NULL);
  sem_init(&m_omx_fill_buffer_done, 0, 0);
}

COMXCoreComponent::~COMXCoreComponent()
{
  Deinitialize();

  pthread_mutex_destroy(&m_omx_input_mutex);
  pthread_mutex_destroy(&m_omx_output_mutex);
  pthread_mutex_destroy(&m_omx_event_mutex);
  pthread_cond_destroy(&m_input_buffer_cond);
  pthread_cond_destroy(&m_output_buffer_cond);
  pthread_cond_destroy(&m_omx_event_cond);

  pthread_mutex_destroy(&m_lock);
  sem_destroy(&m_omx_fill_buffer_done);

  delete m_DllOMX;
}

void COMXCoreComponent::Lock()
{
  pthread_mutex_lock(&m_lock);
}

void COMXCoreComponent::UnLock()
{
  pthread_mutex_unlock(&m_lock);
}

OMX_ERRORTYPE COMXCoreComponent::EmptyThisBuffer(OMX_BUFFERHEADERTYPE *omx_buffer)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(!m_handle || !omx_buffer)
    return OMX_ErrorUndefined;

  omx_err = OMX_EmptyThisBuffer(m_handle, omx_buffer);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::EmptyThisBuffer component(%s) - failed with result(0x%x)\n", 
        m_componentName.c_str(), omx_err);
  }

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::FillThisBuffer(OMX_BUFFERHEADERTYPE *omx_buffer)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(!m_handle || !omx_buffer)
    return OMX_ErrorUndefined;

  omx_err = OMX_FillThisBuffer(m_handle, omx_buffer);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::FillThisBuffer component(%s) - failed with result(0x%x)\n", 
        m_componentName.c_str(), omx_err);
  }

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::FreeOutputBuffer(OMX_BUFFERHEADERTYPE *omx_buffer)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(!m_handle || !omx_buffer)
    return OMX_ErrorUndefined;

  omx_err = OMX_FreeBuffer(m_handle, m_output_port, omx_buffer);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::FreeOutputBuffer component(%s) - failed with result(0x%x)\n",
        m_componentName.c_str(), omx_err);
  }

  return omx_err;
}

unsigned int COMXCoreComponent::GetInputBufferSize()
{
  int free = m_input_buffer_count * m_input_buffer_size;
  return free;
}

unsigned int COMXCoreComponent::GetOutputBufferSize()
{
  int free = m_output_buffer_count * m_output_buffer_size;
  return free;
}

unsigned int COMXCoreComponent::GetInputBufferSpace()
{
  int free = m_omx_input_avaliable.size() * m_input_buffer_size;
  return free;
}

unsigned int COMXCoreComponent::GetOutputBufferSpace()
{
  int free = m_omx_output_available.size() * m_output_buffer_size;
  return free;
}

void COMXCoreComponent::FlushAll()
{
  FlushInput();
  FlushOutput();
}

void COMXCoreComponent::FlushInput()
{
  Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  omx_err = OMX_SendCommand(m_handle, OMX_CommandFlush, m_input_port, NULL);

  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::FlushInput - Error on component %s omx_err(0x%08x)", 
              m_componentName.c_str(), (int)omx_err);
  }
  WaitForCommand(OMX_CommandFlush, m_input_port);

  UnLock();
}

void COMXCoreComponent::FlushOutput()
{
  Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  omx_err = OMX_SendCommand(m_handle, OMX_CommandFlush, m_output_port, NULL);

  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::FlushOutput - Error on component %s omx_err(0x%08x)", 
              m_componentName.c_str(), (int)omx_err);
  }
  WaitForCommand(OMX_CommandFlush, m_output_port);

  UnLock();
}

// timeout in milliseconds
OMX_BUFFERHEADERTYPE *COMXCoreComponent::GetInputBuffer(long timeout)
{
  OMX_BUFFERHEADERTYPE *omx_input_buffer = NULL;

  if(!m_handle)
    return NULL;

  pthread_mutex_lock(&m_omx_input_mutex);
  struct timespec endtime;
  clock_gettime(CLOCK_REALTIME, &endtime);
  add_timespecs(endtime, timeout);
  while (1 && !m_flush_input)
  {
    if(!m_omx_input_avaliable.empty())
    {
      omx_input_buffer = m_omx_input_avaliable.front();
      m_omx_input_avaliable.pop();
      break;
    }

    int retcode = pthread_cond_timedwait(&m_input_buffer_cond, &m_omx_input_mutex, &endtime);
    if (retcode != 0) {
      CLog::Log(LOGERROR, "COMXCoreComponent::GetInputBuffer %s wait event timeout\n", m_componentName.c_str());
      break;
    }
  }
  pthread_mutex_unlock(&m_omx_input_mutex);
  return omx_input_buffer;
}

OMX_BUFFERHEADERTYPE *COMXCoreComponent::GetOutputBuffer()
{
  OMX_BUFFERHEADERTYPE *omx_output_buffer = NULL;

  if(!m_handle)
    return NULL;

  pthread_mutex_lock(&m_omx_output_mutex);

  if(!m_omx_output_available.empty())
  {
    omx_output_buffer = m_omx_output_available.front();
    m_omx_output_available.pop();
  }

  pthread_mutex_unlock(&m_omx_output_mutex);
  return omx_output_buffer;
}

OMX_ERRORTYPE COMXCoreComponent::AllocInputBuffers(bool use_buffers /* = false **/)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  m_omx_input_use_buffers = use_buffers; 

  if(!m_handle)
    return OMX_ErrorUndefined;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = m_input_port;

  omx_err = OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if(omx_err != OMX_ErrorNone)
    return omx_err;

  if(GetState() != OMX_StateIdle)
  {
    if(GetState() != OMX_StateLoaded)
      SetStateForComponent(OMX_StateLoaded);

    SetStateForComponent(OMX_StateIdle);
  }

  omx_err = EnablePort(m_input_port, false);
  if(omx_err != OMX_ErrorNone)
    return omx_err;

  m_input_alignment     = portFormat.nBufferAlignment;
  m_input_buffer_count  = portFormat.nBufferCountActual;
  m_input_buffer_size   = portFormat.nBufferSize;

  CLog::Log(LOGDEBUG, "COMXCoreComponent::AllocInputBuffers component(%s) - port(%d), nBufferCountMin(%lu), nBufferCountActual(%lu), nBufferSize(%lu), nBufferAlignmen(%lu)\n",
            m_componentName.c_str(), GetInputPort(), portFormat.nBufferCountMin,
            portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++)
  {
    OMX_BUFFERHEADERTYPE *buffer = NULL;
    OMX_U8* data = NULL;

    if(m_omx_input_use_buffers)
    {
      data = (OMX_U8*)_aligned_malloc(portFormat.nBufferSize, m_input_alignment);
      omx_err = OMX_UseBuffer(m_handle, &buffer, m_input_port, NULL, portFormat.nBufferSize, data);
    }
    else
    {
      omx_err = OMX_AllocateBuffer(m_handle, &buffer, m_input_port, NULL, portFormat.nBufferSize);
    }
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::AllocInputBuffers component(%s) - OMX_UseBuffer failed with omx_err(0x%x)\n",
        m_componentName.c_str(), omx_err);

      if(m_omx_input_use_buffers && data)
        _aligned_free(data);

      return omx_err;
    }
    buffer->nInputPortIndex = m_input_port;
    buffer->nFilledLen      = 0;
    buffer->nOffset         = 0;
    buffer->pAppPrivate     = (void*)i;  
    m_omx_input_buffers.push_back(buffer);
    m_omx_input_avaliable.push(buffer);
  }

  omx_err = WaitForCommand(OMX_CommandPortEnable, m_input_port);

  m_flush_input = false;

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::AllocOutputBuffers(bool use_buffers /* = false */)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(!m_handle)
    return OMX_ErrorUndefined;

  m_omx_output_use_buffers = use_buffers; 

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = m_output_port;

  omx_err = OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if(omx_err != OMX_ErrorNone)
    return omx_err;

  if(GetState() != OMX_StateIdle)
  {
    if(GetState() != OMX_StateLoaded)
      SetStateForComponent(OMX_StateLoaded);

    SetStateForComponent(OMX_StateIdle);
  }

  omx_err = EnablePort(m_output_port, false);
  if(omx_err != OMX_ErrorNone)
    return omx_err;

  m_output_alignment     = portFormat.nBufferAlignment;
  m_output_buffer_count  = portFormat.nBufferCountActual;
  m_output_buffer_size   = portFormat.nBufferSize;

  CLog::Log(LOGDEBUG, "COMXCoreComponent::AllocOutputBuffers component(%s) - port(%d), nBufferCountMin(%lu), nBufferCountActual(%lu), nBufferSize(%lu) nBufferAlignmen(%lu)\n",
            m_componentName.c_str(), m_output_port, portFormat.nBufferCountMin,
            portFormat.nBufferCountActual, portFormat.nBufferSize, portFormat.nBufferAlignment);

  for (size_t i = 0; i < portFormat.nBufferCountActual; i++)
  {
    OMX_BUFFERHEADERTYPE *buffer = NULL;
    OMX_U8* data = NULL;

    if(m_omx_output_use_buffers)
    {
      data = (OMX_U8*)_aligned_malloc(portFormat.nBufferSize, m_output_alignment);
      omx_err = OMX_UseBuffer(m_handle, &buffer, m_output_port, NULL, portFormat.nBufferSize, data);
    }
    else
    {
      omx_err = OMX_AllocateBuffer(m_handle, &buffer, m_output_port, NULL, portFormat.nBufferSize);
    }
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::AllocOutputBuffers component(%s) - OMX_UseBuffer failed with omx_err(0x%x)\n",
        m_componentName.c_str(), omx_err);

      if(m_omx_output_use_buffers && data)
       _aligned_free(data);

      return omx_err;
    }
    buffer->nOutputPortIndex = m_output_port;
    buffer->nFilledLen       = 0;
    buffer->nOffset          = 0;
    buffer->pAppPrivate      = (void*)i;
    m_omx_output_buffers.push_back(buffer);
    m_omx_output_available.push(buffer);
  }

  omx_err = WaitForCommand(OMX_CommandPortEnable, m_output_port);

  m_flush_output = false;

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::FreeInputBuffers(bool wait)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(!m_handle)
    return OMX_ErrorUndefined;

  if(m_omx_input_buffers.empty())
    return OMX_ErrorNone;

  m_flush_input = true;

  pthread_mutex_lock(&m_omx_input_mutex);
  pthread_cond_broadcast(&m_input_buffer_cond);

  omx_err = DisablePort(m_input_port, false);

  for (size_t i = 0; i < m_omx_input_buffers.size(); i++)
  {
    uint8_t *buf = m_omx_input_buffers[i]->pBuffer;

    omx_err = OMX_FreeBuffer(m_handle, m_input_port, m_omx_input_buffers[i]);

    if(m_omx_input_use_buffers && buf)
      _aligned_free(buf);

    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::FreeInputBuffers error deallocate omx input buffer on component %s omx_err(0x%08x)\n", m_componentName.c_str(), omx_err);
    }
  }

  WaitForCommand(OMX_CommandPortDisable, m_input_port);
  assert(m_omx_input_buffers.size() == m_omx_input_avaliable.size());

  m_omx_input_buffers.clear();

  while (!m_omx_input_avaliable.empty())
    m_omx_input_avaliable.pop();

  m_input_alignment     = 0;
  m_input_buffer_size   = 0;
  m_input_buffer_count  = 0;

  pthread_mutex_unlock(&m_omx_input_mutex);

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::FreeOutputBuffers(bool wait)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(!m_handle)
    return OMX_ErrorUndefined;

  if(m_omx_output_buffers.empty())
    return OMX_ErrorNone;

  m_flush_output = true;

  pthread_mutex_lock(&m_omx_output_mutex);
  pthread_cond_broadcast(&m_output_buffer_cond);

  omx_err = DisablePort(m_output_port, false);

  for (size_t i = 0; i < m_omx_output_buffers.size(); i++)
  {
    uint8_t *buf = m_omx_output_buffers[i]->pBuffer;

    omx_err = OMX_FreeBuffer(m_handle, m_output_port, m_omx_output_buffers[i]);

    if(m_omx_output_use_buffers && buf)
      _aligned_free(buf);

    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::FreeOutputBuffers error deallocate omx output buffer on component %s omx_err(0x%08x)\n", m_componentName.c_str(), omx_err);
    }
  }

  WaitForCommand(OMX_CommandPortDisable, m_output_port);
  assert(m_omx_output_buffers.size() == m_omx_output_available.size());

  m_omx_output_buffers.clear();

  while (!m_omx_output_available.empty())
    m_omx_output_available.pop();

  m_output_alignment    = 0;
  m_output_buffer_size  = 0;
  m_output_buffer_count = 0;

  pthread_mutex_unlock(&m_omx_output_mutex);

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::DisableAllPorts()
{
  Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(!m_handle)
  {
    UnLock();
    return OMX_ErrorUndefined;
  }

  OMX_INDEXTYPE idxTypes[] = {
    OMX_IndexParamAudioInit,
    OMX_IndexParamImageInit,
    OMX_IndexParamVideoInit, 
    OMX_IndexParamOtherInit
  };

  OMX_PORT_PARAM_TYPE ports;
  OMX_INIT_STRUCTURE(ports);

  int i;
  for(i=0; i < 4; i++)
  {
    omx_err = OMX_GetParameter(m_handle, idxTypes[i], &ports);
    if(omx_err == OMX_ErrorNone) {

      uint32_t j;
      for(j=0; j<ports.nPorts; j++)
      {
        OMX_PARAM_PORTDEFINITIONTYPE portFormat;
        OMX_INIT_STRUCTURE(portFormat);
        portFormat.nPortIndex = ports.nStartPortNumber+j;

        omx_err = OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &portFormat);
        if(omx_err != OMX_ErrorNone)
        {
          if(portFormat.bEnabled == OMX_FALSE)
            continue;
        }

        omx_err = OMX_SendCommand(m_handle, OMX_CommandPortDisable, ports.nStartPortNumber+j, NULL);
        if(omx_err != OMX_ErrorNone)
        {
          CLog::Log(LOGERROR, "COMXCoreComponent::DisableAllPorts - Error disable port %d on component %s omx_err(0x%08x)", 
            (int)(ports.nStartPortNumber) + j, m_componentName.c_str(), (int)omx_err);
        }
        omx_err = WaitForCommand(OMX_CommandPortDisable, ports.nStartPortNumber+j);
        if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState)
        {
          UnLock();
          return omx_err;
        }
      }
    }
  }

  UnLock();

  return OMX_ErrorNone;
}

void COMXCoreComponent::Remove(OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2)
{
  for (std::vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); ) 
  {
    omx_event event = *it;

    if(event.eEvent == eEvent && event.nData1 == nData1 && event.nData2 == nData2) 
    {
      it = m_omx_events.erase(it);
      continue;
    }
    ++it;
  }
}

OMX_ERRORTYPE COMXCoreComponent::AddEvent(OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2)
{
  omx_event event;

  event.eEvent      = eEvent;
  event.nData1      = nData1;
  event.nData2      = nData2;

  pthread_mutex_lock(&m_omx_event_mutex);
  Remove(eEvent, nData1, nData2);
  m_omx_events.push_back(event);
  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast(&m_omx_event_cond);
  pthread_mutex_unlock(&m_omx_event_mutex);

#ifdef OMX_DEBUG_EVENTS
  CLog::Log(LOGDEBUG, "COMXCoreComponent::AddEvent %s add event event.eEvent 0x%08x event.nData1 0x%08x event.nData2 %d\n",
          m_componentName.c_str(), (int)event.eEvent, (int)event.nData1, (int)event.nData2);
#endif

  return OMX_ErrorNone;
}

// timeout in milliseconds
OMX_ERRORTYPE COMXCoreComponent::WaitForEvent(OMX_EVENTTYPE eventType, long timeout)
{
#ifdef OMX_DEBUG_EVENTS
  CLog::Log(LOGDEBUG, "COMXCoreComponent::WaitForEvent %s wait event 0x%08x\n",
      m_componentName.c_str(), (int)eventType);
#endif

  pthread_mutex_lock(&m_omx_event_mutex);
  struct timespec endtime;
  clock_gettime(CLOCK_REALTIME, &endtime);
  add_timespecs(endtime, timeout);
  while(true) 
  {
    for (std::vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); it++) 
    {
      omx_event event = *it;

#ifdef OMX_DEBUG_EVENTS
      CLog::Log(LOGDEBUG, "COMXCoreComponent::WaitForEvent %s inlist event event.eEvent 0x%08x event.nData1 0x%08x event.nData2 %d\n",
          m_componentName.c_str(), (int)event.eEvent, (int)event.nData1, (int)event.nData2);
#endif


      if(event.eEvent == OMX_EventError && event.nData1 == (OMX_U32)OMX_ErrorSameState && event.nData2 == 1)
      {
#ifdef OMX_DEBUG_EVENTS
        CLog::Log(LOGDEBUG, "COMXCoreComponent::WaitForEvent %s remove event event.eEvent 0x%08x event.nData1 0x%08x event.nData2 %d\n",
          m_componentName.c_str(), (int)event.eEvent, (int)event.nData1, (int)event.nData2);
#endif
        m_omx_events.erase(it);
        pthread_mutex_unlock(&m_omx_event_mutex);
        return OMX_ErrorNone;
      }
      else if(event.eEvent == OMX_EventError) 
      {
        m_omx_events.erase(it);
        pthread_mutex_unlock(&m_omx_event_mutex);
        return (OMX_ERRORTYPE)event.nData1;
      }
      else if(event.eEvent == eventType) 
      {
#ifdef OMX_DEBUG_EVENTS
        CLog::Log(LOGDEBUG, "COMXCoreComponent::WaitForEvent %s remove event event.eEvent 0x%08x event.nData1 0x%08x event.nData2 %d\n",
          m_componentName.c_str(), (int)event.eEvent, (int)event.nData1, (int)event.nData2);
#endif

        m_omx_events.erase(it);
        pthread_mutex_unlock(&m_omx_event_mutex);
        return OMX_ErrorNone;
      }
    }

    int retcode = pthread_cond_timedwait(&m_omx_event_cond, &m_omx_event_mutex, &endtime);
    if (retcode != 0) 
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::WaitForEvent %s wait event 0x%08x timeout %ld\n",
                          m_componentName.c_str(), (int)eventType, timeout);
      pthread_mutex_unlock(&m_omx_event_mutex);
      return OMX_ErrorMax;
    }
  }
  pthread_mutex_unlock(&m_omx_event_mutex);
  return OMX_ErrorNone;
}

// timeout in milliseconds
OMX_ERRORTYPE COMXCoreComponent::WaitForCommand(OMX_U32 command, OMX_U32 nData2, long timeout)
{
#ifdef OMX_DEBUG_EVENTS
  CLog::Log(LOGDEBUG, "COMXCoreComponent::WaitForCommand %s wait event.eEvent 0x%08x event.command 0x%08x event.nData2 %d\n", 
      m_componentName.c_str(), (int)OMX_EventCmdComplete, (int)command, (int)nData2);
#endif

  pthread_mutex_lock(&m_omx_event_mutex);
  struct timespec endtime;
  clock_gettime(CLOCK_REALTIME, &endtime);
  add_timespecs(endtime, timeout);
  while(true) 
  {
    for (std::vector<omx_event>::iterator it = m_omx_events.begin(); it != m_omx_events.end(); it++) 
    {
      omx_event event = *it;

#ifdef OMX_DEBUG_EVENTS
      CLog::Log(LOGDEBUG, "COMXCoreComponent::WaitForCommand %s inlist event event.eEvent 0x%08x event.nData1 0x%08x event.nData2 %d\n",
          m_componentName.c_str(), (int)event.eEvent, (int)event.nData1, (int)event.nData2);
#endif
      if(event.eEvent == OMX_EventError && event.nData1 == (OMX_U32)OMX_ErrorSameState && event.nData2 == 1)
      {
#ifdef OMX_DEBUG_EVENTS
        CLog::Log(LOGDEBUG, "COMXCoreComponent::WaitForCommand %s remove event event.eEvent 0x%08x event.nData1 0x%08x event.nData2 %d\n",
          m_componentName.c_str(), (int)event.eEvent, (int)event.nData1, (int)event.nData2);
#endif

        m_omx_events.erase(it);
        pthread_mutex_unlock(&m_omx_event_mutex);
        return OMX_ErrorNone;
      } 
      else if(event.eEvent == OMX_EventError) 
      {
        m_omx_events.erase(it);
        pthread_mutex_unlock(&m_omx_event_mutex);
        return (OMX_ERRORTYPE)event.nData1;
      } 
      else if(event.eEvent == OMX_EventCmdComplete && event.nData1 == command && event.nData2 == nData2) 
      {

#ifdef OMX_DEBUG_EVENTS
        CLog::Log(LOGDEBUG, "COMXCoreComponent::WaitForCommand %s remove event event.eEvent 0x%08x event.nData1 0x%08x event.nData2 %d\n",
          m_componentName.c_str(), (int)event.eEvent, (int)event.nData1, (int)event.nData2);
#endif

        m_omx_events.erase(it);
        pthread_mutex_unlock(&m_omx_event_mutex);
        return OMX_ErrorNone;
      }
    }

    int retcode = pthread_cond_timedwait(&m_omx_event_cond, &m_omx_event_mutex, &endtime);
    if (retcode != 0) {
      CLog::Log(LOGERROR, "COMXCoreComponent::WaitForCommand %s wait timeout event.eEvent 0x%08x event.command 0x%08x event.nData2 %d\n", 
        m_componentName.c_str(), (int)OMX_EventCmdComplete, (int)command, (int)nData2);
      
      pthread_mutex_unlock(&m_omx_event_mutex);
      return OMX_ErrorMax;
    }
  }
  pthread_mutex_unlock(&m_omx_event_mutex);
  return OMX_ErrorNone;
}

OMX_ERRORTYPE COMXCoreComponent::SetStateForComponent(OMX_STATETYPE state)
{
  Lock();
  
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_STATETYPE state_actual = OMX_StateMax;

  if(!m_handle)
  {
    UnLock();
    return OMX_ErrorUndefined;
  }

  OMX_GetState(m_handle, &state_actual);
  if(state == state_actual)
  {
    UnLock();
    return OMX_ErrorNone;
  }

  omx_err = OMX_SendCommand(m_handle, OMX_CommandStateSet, state, 0);
  if (omx_err != OMX_ErrorNone)
  {
    if(omx_err == OMX_ErrorSameState)
    {
      omx_err = OMX_ErrorNone;
    }
    else
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::SetStateForComponent - %s failed with omx_err(0x%x)\n", 
        m_componentName.c_str(), omx_err);
    }
  }
  else 
  {
    omx_err = WaitForCommand(OMX_CommandStateSet, state);
    if(omx_err == OMX_ErrorSameState)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::SetStateForComponent - %s ignore OMX_ErrorSameState\n", 
        m_componentName.c_str());
      UnLock();
      return OMX_ErrorNone;
    }
  }

  UnLock();

  return omx_err;
}

OMX_STATETYPE COMXCoreComponent::GetState()
{
  Lock();

  OMX_STATETYPE state;

  if(m_handle)
  {
    OMX_GetState(m_handle, &state);
    UnLock();
    return state;
  }

  UnLock();

  return (OMX_STATETYPE)0;
}

OMX_ERRORTYPE COMXCoreComponent::SetParameter(OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct)
{
  Lock();

  OMX_ERRORTYPE omx_err;

  omx_err = OMX_SetParameter(m_handle, paramIndex, paramStruct);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::SetParameter - %s failed with omx_err(0x%x)\n", 
              m_componentName.c_str(), omx_err);
  }

  UnLock();

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::GetParameter(OMX_INDEXTYPE paramIndex, OMX_PTR paramStruct)
{
  Lock();

  OMX_ERRORTYPE omx_err;

  omx_err = OMX_GetParameter(m_handle, paramIndex, paramStruct);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::GetParameter - %s failed with omx_err(0x%x)\n", 
              m_componentName.c_str(), omx_err);
  }

  UnLock();

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::SetConfig(OMX_INDEXTYPE configIndex, OMX_PTR configStruct)
{
  Lock();

  OMX_ERRORTYPE omx_err;

  omx_err = OMX_SetConfig(m_handle, configIndex, configStruct);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n", 
              m_componentName.c_str(), omx_err);
  }

  UnLock();

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::GetConfig(OMX_INDEXTYPE configIndex, OMX_PTR configStruct)
{
  Lock();

  OMX_ERRORTYPE omx_err;

  omx_err = OMX_GetConfig(m_handle, configIndex, configStruct);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::GetConfig - %s failed with omx_err(0x%x)\n", 
              m_componentName.c_str(), omx_err);
  }

  UnLock();

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::SendCommand(OMX_COMMANDTYPE cmd, OMX_U32 cmdParam, OMX_PTR cmdParamData)
{
  Lock();

  OMX_ERRORTYPE omx_err;

  omx_err = OMX_SendCommand(m_handle, cmd, cmdParam, cmdParamData);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::SendCommand - %s failed with omx_err(0x%x)\n", 
              m_componentName.c_str(), omx_err);
  }

  UnLock();

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::EnablePort(unsigned int port,  bool wait)
{
  Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = port;

  omx_err = OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::EnablePort - Error get port %d status on component %s omx_err(0x%08x)", 
        port, m_componentName.c_str(), (int)omx_err);
  }

  if(portFormat.bEnabled == OMX_FALSE)
  {
    omx_err = OMX_SendCommand(m_handle, OMX_CommandPortEnable, port, NULL);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::EnablePort - Error enable port %d on component %s omx_err(0x%08x)", 
          port, m_componentName.c_str(), (int)omx_err);
      {
        UnLock();
        return omx_err;
      }
    }
    else
    {
      if(wait)
        omx_err = WaitForCommand(OMX_CommandPortEnable, port);
    }
  }

  UnLock();

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::DisablePort(unsigned int port, bool wait)
{
  Lock();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  OMX_PARAM_PORTDEFINITIONTYPE portFormat;
  OMX_INIT_STRUCTURE(portFormat);
  portFormat.nPortIndex = port;

  omx_err = OMX_GetParameter(m_handle, OMX_IndexParamPortDefinition, &portFormat);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::DisablePort - Error get port %d status on component %s omx_err(0x%08x)", 
        port, m_componentName.c_str(), (int)omx_err);
  }

  if(portFormat.bEnabled == OMX_TRUE)
  {
    omx_err = OMX_SendCommand(m_handle, OMX_CommandPortDisable, port, NULL);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::DIsablePort - Error disable port %d on component %s omx_err(0x%08x)", 
          port, m_componentName.c_str(), (int)omx_err);
      {
        UnLock();
        return omx_err;
      }
    }
    else
    {
      if(wait)
        omx_err = WaitForCommand(OMX_CommandPortDisable, port);
    }
  }

  UnLock();

  return omx_err;
}

OMX_ERRORTYPE COMXCoreComponent::UseEGLImage(OMX_BUFFERHEADERTYPE** ppBufferHdr, OMX_U32 nPortIndex, OMX_PTR pAppPrivate, void* eglImage)
{
  Lock();

  OMX_ERRORTYPE omx_err;

  omx_err = OMX_UseEGLImage(m_handle, ppBufferHdr, nPortIndex, pAppPrivate, eglImage);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::UseEGLImage - %s failed with omx_err(0x%x)\n", 
              m_componentName.c_str(), omx_err);
  }

  UnLock();

  return omx_err;
}

bool COMXCoreComponent::Initialize( const std::string &component_name, OMX_INDEXTYPE index)
{
  OMX_ERRORTYPE omx_err;

  if(!m_DllOMX->Load())
    return false;

  m_DllOMXOpen = true;

  m_componentName = component_name;
  
  m_callbacks.EventHandler    = &COMXCoreComponent::DecoderEventHandlerCallback;
  m_callbacks.EmptyBufferDone = &COMXCoreComponent::DecoderEmptyBufferDoneCallback;
  m_callbacks.FillBufferDone  = &COMXCoreComponent::DecoderFillBufferDoneCallback;

  // Get video component handle setting up callbacks, component is in loaded state on return.
  omx_err = m_DllOMX->OMX_GetHandle(&m_handle, (char*)component_name.c_str(), this, &m_callbacks);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::Initialize - could not get component handle for %s omx_err(0x%08x)\n", 
        component_name.c_str(), (int)omx_err);
    Deinitialize();
    return false;
  }

  OMX_PORT_PARAM_TYPE port_param;
  OMX_INIT_STRUCTURE(port_param);

  omx_err = OMX_GetParameter(m_handle, index, &port_param);
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::Initialize - could not get port_param for component %s omx_err(0x%08x)\n", 
        component_name.c_str(), (int)omx_err);
  }

  omx_err = DisableAllPorts();
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCoreComponent::Initialize - error disable ports on component %s omx_err(0x%08x)\n", 
        component_name.c_str(), (int)omx_err);
  }

  m_input_port  = port_param.nStartPortNumber;
  m_output_port = m_input_port + 1;

  if(m_componentName == "OMX.broadcom.audio_mixer")
  {
    m_input_port  = port_param.nStartPortNumber + 1;
    m_output_port = port_param.nStartPortNumber;
  }

  if (m_output_port > port_param.nStartPortNumber+port_param.nPorts-1)
    m_output_port = port_param.nStartPortNumber+port_param.nPorts-1;

  CLog::Log(LOGDEBUG, "COMXCoreComponent::Initialize %s input port %d output port %d\n",
      m_componentName.c_str(), m_input_port, m_output_port);

  m_exit = false;
  m_flush_input   = false;
  m_flush_output  = false;

  return true;
}

bool COMXCoreComponent::Deinitialize()
{
  OMX_ERRORTYPE omx_err;

  if(!m_DllOMXOpen)
    return false;

  m_exit = true;

  m_flush_input   = true;
  m_flush_output  = true;

  if(m_handle) 
  {

    FlushAll();

    FreeOutputBuffers(true);
    FreeInputBuffers(true);

    if(GetState() == OMX_StateExecuting)
      SetStateForComponent(OMX_StatePause);

    if(GetState() != OMX_StateIdle)
      SetStateForComponent(OMX_StateIdle);

    if(GetState() != OMX_StateLoaded)
      SetStateForComponent(OMX_StateLoaded);

    omx_err = m_DllOMX->OMX_FreeHandle(m_handle);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCoreComponent::Deinitialize - failed to free handle for component %s omx_err(0x%08x)", 
          m_componentName.c_str(), omx_err);
    }  

    m_handle = NULL;
  }

  m_input_port    = 0;
  m_output_port   = 0;
  m_componentName = "";
  m_DllOMXOpen    = false;

  CustomDecoderFillBufferDoneHandler = NULL;
  CustomDecoderEmptyBufferDoneHandler = NULL;

  m_DllOMX->Unload();

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////
// DecoderEventHandler -- OMX event callback
OMX_ERRORTYPE COMXCoreComponent::DecoderEventHandlerCallback(
  OMX_HANDLETYPE hComponent,
  OMX_PTR pAppData,
  OMX_EVENTTYPE eEvent,
  OMX_U32 nData1,
  OMX_U32 nData2,
  OMX_PTR pEventData)
{
  if(!pAppData)
    return OMX_ErrorNone;

  COMXCoreComponent *ctx = static_cast<COMXCoreComponent*>(pAppData);
  return ctx->DecoderEventHandler(hComponent, pAppData, eEvent, nData1, nData2, pEventData);
}

// DecoderEmptyBufferDone -- OMXCore input buffer has been emptied
OMX_ERRORTYPE COMXCoreComponent::DecoderEmptyBufferDoneCallback(
  OMX_HANDLETYPE hComponent,
  OMX_PTR pAppData,
  OMX_BUFFERHEADERTYPE* pBuffer)
{
  if(!pAppData)
    return OMX_ErrorNone;

  COMXCoreComponent *ctx = static_cast<COMXCoreComponent*>(pAppData);

  if(ctx->CustomDecoderEmptyBufferDoneHandler){
    OMX_ERRORTYPE omx_err = (*(ctx->CustomDecoderEmptyBufferDoneHandler))(hComponent, pAppData, pBuffer);
    if(omx_err != OMX_ErrorNone)return omx_err;
  }

  return ctx->DecoderEmptyBufferDone( hComponent, pAppData, pBuffer);
}

// DecoderFillBufferDone -- OMXCore output buffer has been filled
OMX_ERRORTYPE COMXCoreComponent::DecoderFillBufferDoneCallback(
  OMX_HANDLETYPE hComponent,
  OMX_PTR pAppData,
  OMX_BUFFERHEADERTYPE* pBuffer)
{
  if(!pAppData)
    return OMX_ErrorNone;

  COMXCoreComponent *ctx = static_cast<COMXCoreComponent*>(pAppData);
 
  if(ctx->CustomDecoderFillBufferDoneHandler){
    OMX_ERRORTYPE omx_err = (*(ctx->CustomDecoderFillBufferDoneHandler))(hComponent, pAppData, pBuffer);
    if(omx_err != OMX_ErrorNone)return omx_err;
  }

  return ctx->DecoderFillBufferDone(hComponent, pAppData, pBuffer);
}

OMX_ERRORTYPE COMXCoreComponent::DecoderEmptyBufferDone(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
  if(!pAppData || m_exit)
    return OMX_ErrorNone;

  COMXCoreComponent *ctx = static_cast<COMXCoreComponent*>(pAppData);

  pthread_mutex_lock(&ctx->m_omx_input_mutex);
  ctx->m_omx_input_avaliable.push(pBuffer);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast(&ctx->m_input_buffer_cond);

  pthread_mutex_unlock(&ctx->m_omx_input_mutex);

  return OMX_ErrorNone;
}

OMX_ERRORTYPE COMXCoreComponent::DecoderFillBufferDone(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
  if(!pAppData || m_exit)
    return OMX_ErrorNone;
  
  COMXCoreComponent *ctx = static_cast<COMXCoreComponent*>(pAppData);

  pthread_mutex_lock(&ctx->m_omx_output_mutex);
  ctx->m_omx_output_available.push(pBuffer);

  // this allows (all) blocked tasks to be awoken
  pthread_cond_broadcast(&ctx->m_output_buffer_cond);

  pthread_mutex_unlock(&ctx->m_omx_output_mutex);

  sem_post(&ctx->m_omx_fill_buffer_done);

  return OMX_ErrorNone;
}

// DecoderEmptyBufferDone -- OMXCore input buffer has been emptied
////////////////////////////////////////////////////////////////////////////////////////////
// Component event handler -- OMX event callback
OMX_ERRORTYPE COMXCoreComponent::DecoderEventHandler(
  OMX_HANDLETYPE hComponent,
  OMX_PTR pAppData,
  OMX_EVENTTYPE eEvent,
  OMX_U32 nData1,
  OMX_U32 nData2,
  OMX_PTR pEventData)
{
  COMXCoreComponent *ctx = static_cast<COMXCoreComponent*>(pAppData);

#ifdef OMX_DEBUG_EVENTS
  CLog::Log(LOGDEBUG,
    "COMXCore::%s - %s eEvent(0x%x), nData1(0x%lx), nData2(0x%lx), pEventData(0x%p)\n",
    __func__, (char *)ctx->GetName().c_str(), eEvent, nData1, nData2, pEventData);
#endif

  AddEvent(eEvent, nData1, nData2);

  switch (eEvent)
  {
    case OMX_EventCmdComplete:
      
      switch(nData1)
      {
        case OMX_CommandStateSet:
          switch ((int)nData2)
          {
            case OMX_StateInvalid:
            #if defined(OMX_DEBUG_EVENTHANDLER)
              CLog::Log(LOGDEBUG, "%s::%s %s - OMX_StateInvalid\n", CLASSNAME, __func__, ctx->GetName().c_str());
            #endif
            break;
            case OMX_StateLoaded:
            #if defined(OMX_DEBUG_EVENTHANDLER)
              CLog::Log(LOGDEBUG, "%s::%s %s - OMX_StateLoaded\n", CLASSNAME, __func__, ctx->GetName().c_str());
            #endif
            break;
            case OMX_StateIdle:
            #if defined(OMX_DEBUG_EVENTHANDLER)
              CLog::Log(LOGDEBUG, "%s::%s %s - OMX_StateIdle\n", CLASSNAME, __func__, ctx->GetName().c_str());
            #endif
            break;
            case OMX_StateExecuting:
            #if defined(OMX_DEBUG_EVENTHANDLER)
              CLog::Log(LOGDEBUG, "%s::%s %s - OMX_StateExecuting\n", CLASSNAME, __func__, ctx->GetName().c_str());
            #endif
            break;
            case OMX_StatePause:
            #if defined(OMX_DEBUG_EVENTHANDLER)
              CLog::Log(LOGDEBUG, "%s::%s %s - OMX_StatePause\n", CLASSNAME, __func__, ctx->GetName().c_str());
            #endif
            break;
            case OMX_StateWaitForResources:
            #if defined(OMX_DEBUG_EVENTHANDLER)
              CLog::Log(LOGDEBUG, "%s::%s %s - OMX_StateWaitForResources\n", CLASSNAME, __func__, ctx->GetName().c_str());
            #endif
            break;
            default:
            #if defined(OMX_DEBUG_EVENTHANDLER)
              CLog::Log(LOGDEBUG,
                "%s::%s %s - Unknown OMX_Statexxxxx, state(%d)\n", CLASSNAME, __func__, ctx->GetName().c_str(), (int)nData2);
            #endif
            break;
          }
        break;
        case OMX_CommandFlush:
          #if defined(OMX_DEBUG_EVENTHANDLER)
          CLog::Log(LOGDEBUG, "%s::%s %s - OMX_CommandFlush, port %d\n", CLASSNAME, __func__, ctx->GetName().c_str(), (int)nData2);
          #endif
        break;
        case OMX_CommandPortDisable:
          #if defined(OMX_DEBUG_EVENTHANDLER)
          CLog::Log(LOGDEBUG, "%s::%s %s - OMX_CommandPortDisable, nData1(0x%lx), port %d\n", CLASSNAME, __func__, ctx->GetName().c_str(), nData1, (int)nData2);
          #endif
        break;
        case OMX_CommandPortEnable:
          #if defined(OMX_DEBUG_EVENTHANDLER)
          CLog::Log(LOGDEBUG, "%s::%s %s - OMX_CommandPortEnable, nData1(0x%lx), port %d\n", CLASSNAME, __func__, ctx->GetName().c_str(), nData1, (int)nData2);
          #endif
        break;
        #if defined(OMX_DEBUG_EVENTHANDLER)
        case OMX_CommandMarkBuffer:
          CLog::Log(LOGDEBUG, "%s::%s %s - OMX_CommandMarkBuffer, nData1(0x%lx), port %d\n", CLASSNAME, __func__, ctx->GetName().c_str(), nData1, (int)nData2);
        break;
        #endif
      }
    break;
    case OMX_EventBufferFlag:
      #if defined(OMX_DEBUG_EVENTHANDLER)
      CLog::Log(LOGDEBUG, "%s::%s %s - OMX_EventBufferFlag(input)\n", CLASSNAME, __func__, ctx->GetName().c_str());
      #endif
      if(nData2 & OMX_BUFFERFLAG_EOS)
        ctx->m_eos = true;
    break;
    case OMX_EventPortSettingsChanged:
      #if defined(OMX_DEBUG_EVENTHANDLER)
      CLog::Log(LOGDEBUG, "%s::%s %s - OMX_EventPortSettingsChanged(output)\n", CLASSNAME, __func__, ctx->GetName().c_str());
      #endif
    break;
    #if defined(OMX_DEBUG_EVENTHANDLER)
    case OMX_EventMark:
      CLog::Log(LOGDEBUG, "%s::%s %s - OMX_EventMark\n", CLASSNAME, __func__, ctx->GetName().c_str());
    break;
    case OMX_EventResourcesAcquired:
      CLog::Log(LOGDEBUG, "%s::%s %s- OMX_EventResourcesAcquired\n", CLASSNAME, __func__, ctx->GetName().c_str());
    break;
    #endif
    case OMX_EventError:
      switch((OMX_S32)nData1)
      {
        case OMX_ErrorSameState:
        break;
        case OMX_ErrorInsufficientResources:
          CLog::Log(LOGERROR, "%s::%s %s - OMX_ErrorInsufficientResources, insufficient resources\n", CLASSNAME, __func__, ctx->GetName().c_str());
        break;
        case OMX_ErrorFormatNotDetected:
          CLog::Log(LOGERROR, "%s::%s %s - OMX_ErrorFormatNotDetected, cannot parse input stream\n", CLASSNAME, __func__, ctx->GetName().c_str());
        break;
        case OMX_ErrorPortUnpopulated:
          CLog::Log(LOGERROR, "%s::%s %s - OMX_ErrorPortUnpopulated port %d, cannot parse input stream\n", CLASSNAME, __func__, ctx->GetName().c_str(), (int)nData2);
        break;
        case OMX_ErrorStreamCorrupt:
          CLog::Log(LOGERROR, "%s::%s %s - OMX_ErrorStreamCorrupt, Bitstream corrupt\n", CLASSNAME, __func__, ctx->GetName().c_str());
        break;
        default:
          CLog::Log(LOGERROR, "%s::%s %s - OMX_EventError detected, nData1(0x%lx), port %d\n",  CLASSNAME, __func__, ctx->GetName().c_str(), nData1, (int)nData2);
        break;
      }
      sem_post(&ctx->m_omx_fill_buffer_done);
    break;
    default:
      CLog::Log(LOGWARNING, "%s::%s %s - Unknown eEvent(0x%x), nData1(0x%lx), port %d\n", CLASSNAME, __func__, ctx->GetName().c_str(), eEvent, nData1, (int)nData2);
    break;
  }

  return OMX_ErrorNone;
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
COMXCore::COMXCore()
{
  m_is_open = false;

  m_DllOMX  = new DllOMX();
}

COMXCore::~COMXCore()
{
  delete m_DllOMX;
}

bool COMXCore::Initialize()
{
  if(!m_DllOMX->Load())
    return false;

  OMX_ERRORTYPE omx_err = m_DllOMX->OMX_Init();
  if (omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXCore::Initialize - OMXCore failed to init, omx_err(0x%08x)", omx_err);
    return false;
  }

  m_is_open = true;
  return true;
}

void COMXCore::Deinitialize()
{
  if(m_is_open)
  {
    OMX_ERRORTYPE omx_err = m_DllOMX->OMX_Deinit();
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXCore::Deinitialize - OMXCore failed to deinit, omx_err(0x%08x)", omx_err);
    }  
    m_DllOMX->Unload();
  }
}

#endif
