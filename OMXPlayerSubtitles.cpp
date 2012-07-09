/*
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
#include "utils/log.h"

#include <boost/algorithm/string.hpp>

constexpr int RENDER_LOOP_SLEEP = 16;

OMXPlayerSubtitles::OMXPlayerSubtitles() noexcept:
#ifndef NDEBUG
m_open(),
#endif
m_subtitle_queue(32),
m_abort(true),
m_flush(false)
{}

OMXPlayerSubtitles::~OMXPlayerSubtitles() noexcept
{
  Close();
}

bool OMXPlayerSubtitles::
Open(const std::string& font_path, float font_size, OMXClock* clock) noexcept
{
  assert(!m_open);

  m_abort.store(false, std::memory_order_relaxed);
  m_flush.store(false, std::memory_order_relaxed);

  try
  {
    m_rendering_thread = std::thread([=]
    {
      try
      {
        Process(font_path, font_size, clock);
      }
      catch(std::exception& e)
      {
        m_abort.store(true, std::memory_order_relaxed);
        CLog::Log(LOGERROR, "OMXPlayerSubtitles::Process threw: %s", e.what());
      }
    });
  }
  catch (...)
  {
    return false;
  }

#ifndef NDEBUG
  m_open = true;
#endif

  return true;
}

void OMXPlayerSubtitles::Close() noexcept
{
  if (m_rendering_thread.joinable())
  {
    m_abort.store(true, std::memory_order_relaxed);
    try
    {
      m_rendering_thread.join();
    }
    catch (std::exception& e)
    {
      CLog::Log(LOGERROR,
                "OMXPlayerSubtitles rendering thread failed to join: %s",
                e.what());
    }
  }

#ifndef NDEBUG
  m_open = false;
#endif
}

void OMXPlayerSubtitles::
Process(const std::string& font_path, float font_size, OMXClock* clock)
{
  SubtitleRenderer renderer(1, font_path, font_size, 0.01f, 0.06f, 0xDD, 0x80);

  Subtitle* next_subtitle{};
  double current_stop{};
  bool showing{};

  while (!m_abort.load(std::memory_order_relaxed))
  {
    if (m_flush.load(std::memory_order_relaxed))
    {
      if (showing)
      {
        renderer.unprepare();
        renderer.hide();
        showing = false;
      }
      next_subtitle = NULL;
      while (!m_subtitle_queue.isEmpty())
        m_subtitle_queue.popFront();
      m_flush.store(false, std::memory_order_relaxed);
    }

    if (!next_subtitle)
    {
      next_subtitle = m_subtitle_queue.frontPtr();
      if (next_subtitle)
        renderer.prepare(next_subtitle->text_lines);
    }

    auto const now = clock->OMXMediaTime();

    if (next_subtitle && next_subtitle->start <= now)
    {
      renderer.show_next();
      showing = true;

      current_stop = next_subtitle->stop;
      next_subtitle = NULL;
      m_subtitle_queue.popFront();
      continue;
    }

    if (showing && current_stop <= now)
    {
      renderer.hide();
      showing = false;
      continue;
    }

    clock->OMXSleep(RENDER_LOOP_SLEEP);
  }
}

void OMXPlayerSubtitles::Flush() noexcept
{
  assert(m_open);

  m_flush.store(true, std::memory_order_relaxed);
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
  if (e && e->IsElementType(COMXOverlayText::ELEMENT_TYPE_TEXT))
  {
    boost::split(text_lines,
                 ((COMXOverlayText::CElementText*) e)->m_text,
                 boost::is_any_of("\r\n"),
                 boost::token_compress_on);
  }

  return text_lines;
}

bool OMXPlayerSubtitles::AddPacket(OMXPacket *pkt) noexcept
{
  assert(m_open);

  if (!pkt)
    return false;

  if (m_abort.load(std::memory_order_relaxed))
  {
    // Rendering thread has stopped, throw away the packet
    OMXReader::FreePacket(pkt);
    return true;
  }

  if (m_flush.load(std::memory_order_relaxed))
    return false;

  // Center the presentation time on the requested timestamps
  const auto adjusted_start = pkt->pts - (RENDER_LOOP_SLEEP*1000/2);

  auto result = m_subtitle_queue.write(
    Subtitle{adjusted_start, adjusted_start+pkt->duration, GetTextLines(pkt)});

  if (result)
    OMXReader::FreePacket(pkt);
  return result;
}
