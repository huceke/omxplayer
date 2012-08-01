/*
 * Author: Torarin Hals Bakke (2012)
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

#include "OMXPlayerSubtitles.h"
#include "OMXOverlayText.h"
#include "SubtitleRenderer.h"
#include "LockBlock.h"
#include "Enforce.h"
#include "utils/log.h"

#include <boost/algorithm/string.hpp>
#include <typeinfo>

constexpr int RENDER_LOOP_SLEEP = 100;

OMXPlayerSubtitles::OMXPlayerSubtitles() BOOST_NOEXCEPT
:
#ifndef NDEBUG
  m_open(),
#endif
  m_subtitle_queue(32),
  m_thread_stopped(true),
  m_flush(),
  m_font_size(),
  m_centered(),
  m_av_clock()
{}

OMXPlayerSubtitles::~OMXPlayerSubtitles() BOOST_NOEXCEPT
{
  Close();
}

bool OMXPlayerSubtitles::
Open(const std::string& font_path, float font_size, bool centered, OMXClock* clock) BOOST_NOEXCEPT
{
  assert(!m_open);

  m_thread_stopped.store(false, std::memory_order_relaxed);
  m_flush.store(false, std::memory_order_relaxed);
  m_font_path = font_path;
  m_font_size = font_size;
  m_centered = centered;
  m_av_clock  = clock;

  if(!Create())
    return false;

#ifndef NDEBUG
  m_open = true;
#endif

  return true;
}

void OMXPlayerSubtitles::Close() BOOST_NOEXCEPT
{
  if(Running())
    StopThread();

#ifndef NDEBUG
  m_open = false;
#endif
}

void OMXPlayerSubtitles::Process()
{
  try
  {
    RenderLoop(m_font_path, m_font_size, m_centered, m_av_clock);
  }
  catch(Enforce_error& e)
  {
    if(!e.user_friendly_what().empty())
      printf("Error: %s\n", e.user_friendly_what().c_str());
    CLog::Log(LOGERROR, "OMXPlayerSubtitles::RenderLoop threw %s (%s)",
              typeid(e).name(), e.what());
  }
  catch(std::exception& e)
  {
    CLog::Log(LOGERROR, "OMXPlayerSubtitles::RenderLoop threw %s (%s)",
              typeid(e).name(), e.what());
  }
  m_thread_stopped.store(true, std::memory_order_relaxed);
}

void OMXPlayerSubtitles::
RenderLoop(const std::string& font_path, float font_size, bool centered, OMXClock* clock)
{
  SubtitleRenderer renderer(1,
                            font_path,
                            font_size,
                            0.01f, 0.06f,
                            centered,
                            0xDD,
                            0x80);

  Subtitle* next_subtitle{};
  double current_stop{};
  bool showing{};

  while(!m_bStop)
  {
    if(m_flush.load(std::memory_order_acquire))
    {
      if(showing)
      {
        renderer.unprepare();
        renderer.hide();
        showing = false;
      }
      next_subtitle = NULL;
      m_subtitle_queue.clear(); // safe
      m_flush.store(false, std::memory_order_release);

      OMXClock::OMXSleep(RENDER_LOOP_SLEEP);
      continue;
    }

    auto const now = clock->OMXMediaTime();

    if(next_subtitle && next_subtitle->stop <= now)
    {
      renderer.unprepare();
      next_subtitle = NULL;
      LOCK_BLOCK(m_subtitle_queue_lock)
        m_subtitle_queue.pop_front();     
    }

    if(!next_subtitle)
    {
      LOCK_BLOCK(m_subtitle_queue_lock)
      {
        for(; !m_subtitle_queue.empty(); m_subtitle_queue.pop_front())
        {
          if(m_subtitle_queue.front().stop > now)
          {
            next_subtitle = &m_subtitle_queue.front();
            break;
          }
        }
      }
      if(next_subtitle)
        renderer.prepare(next_subtitle->text_lines);
    }

    if(next_subtitle && next_subtitle->start <= now)
    {
      renderer.show_next();
      showing = true;

      current_stop = next_subtitle->stop;
      next_subtitle = NULL;
      LOCK_BLOCK(m_subtitle_queue_lock)
        m_subtitle_queue.pop_front();
    }
    else if(showing && current_stop <= now)
    {
        renderer.hide();
        showing = false;
    }
    else
    {
      OMXClock::OMXSleep(RENDER_LOOP_SLEEP);
    }
  }
}

void OMXPlayerSubtitles::Flush() BOOST_NOEXCEPT
{
  assert(m_open);

  m_flush.store(true, std::memory_order_release);
}

std::vector<std::string> OMXPlayerSubtitles::GetTextLines(OMXPacket *pkt)
{
  assert(pkt);

  m_subtitle_codec.Open(pkt->hints);

  auto result = m_subtitle_codec.Decode(pkt->data, pkt->size, 0, 0);
  assert(result == OC_OVERLAY);

  auto overlay = m_subtitle_codec.GetOverlay();
  assert(overlay);

  std::vector<std::string> text_lines;

  auto e = ((COMXOverlayText*) overlay)->m_pHead;
  if(e && e->IsElementType(COMXOverlayText::ELEMENT_TYPE_TEXT))
  {
    boost::split(text_lines,
                 ((COMXOverlayText::CElementText*) e)->m_text,
                 boost::is_any_of("\r\n"),
                 boost::token_compress_on);
  }

  return text_lines;
}

bool OMXPlayerSubtitles::AddPacket(OMXPacket *pkt) BOOST_NOEXCEPT
{
  assert(m_open);

  if(!pkt)
    return false;

  if(m_thread_stopped.load(std::memory_order_relaxed))
  {
    // Rendering thread has stopped, throw away the packet
    CLog::Log(LOGWARNING, "Subtitle rendering thread has stopped, discarding packet");
    OMXReader::FreePacket(pkt);
    return true;
  }

  if(m_flush.load(std::memory_order_acquire))
    return false;

  bool queue_full;
  LOCK_BLOCK(m_subtitle_queue_lock)
    queue_full = m_subtitle_queue.full();

  if(queue_full) 
  {
    // Packets coming ridiculously early, throw them away
    CLog::Log(LOGWARNING, "Subtitle queue full, dropping packet");
    OMXReader::FreePacket(pkt);
    return true;
  }

  // Center the presentation time on the requested timestamps
  auto adjusted_start = pkt->pts - RENDER_LOOP_SLEEP*1000/2;

  LOCK_BLOCK(m_subtitle_queue_lock)
  {
    m_subtitle_queue.push_back({adjusted_start,
                                adjusted_start + pkt->duration,
                                GetTextLines(pkt)});

  }

  OMXReader::FreePacket(pkt);
  return true;
}
