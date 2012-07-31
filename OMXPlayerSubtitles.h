#pragma once

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

#include "OMXThread.h"
#include "OMXReader.h"
#include "OMXClock.h"
#include "OMXOverlayCodecText.h"

#include <boost/config.hpp>
#include <boost/circular_buffer.hpp>
#include <atomic>
#include <string>
#include <deque>
#include <mutex>

class OMXPlayerSubtitles : public OMXThread
{
public:
  OMXPlayerSubtitles(const OMXPlayerSubtitles&) = delete;
  OMXPlayerSubtitles& operator=(const OMXPlayerSubtitles&) = delete;
  OMXPlayerSubtitles() BOOST_NOEXCEPT;
  ~OMXPlayerSubtitles() BOOST_NOEXCEPT;
  bool Open(const std::string& font_path, float font_size, bool centered, OMXClock* clock) BOOST_NOEXCEPT;
  void Close() BOOST_NOEXCEPT;
  void Flush() BOOST_NOEXCEPT;
  bool AddPacket(OMXPacket *pkt) BOOST_NOEXCEPT;

private:
  struct Subtitle
  {
    double start;
    double stop;
    std::vector<std::string> text_lines;
  };

  void Process();
  void RenderLoop(const std::string& font_path, float font_size, bool centered, OMXClock* clock);
  std::vector<std::string> GetTextLines(OMXPacket *pkt);

#ifndef NDEBUG
  bool m_open;
#endif

  COMXOverlayCodecText                   m_subtitle_codec;
  boost::circular_buffer<Subtitle>       m_subtitle_queue;
  std::mutex                             m_subtitle_queue_lock;
  std::atomic<bool>                      m_thread_stopped;
  std::atomic<bool>                      m_flush;
  std::string                            m_font_path;
  float                                  m_font_size;
  bool                                   m_centered;
  OMXClock*                              m_av_clock;
};
