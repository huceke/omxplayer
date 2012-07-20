#pragma once

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

#include "OMXThread.h"
#include "OMXReader.h"
#include "OMXClock.h"
#include "OMXOverlayCodecText.h"
#include "folly/ProducerConsumerQueue.h"
#include "folly/Workarounds.h"

#include <string>

class OMXPlayerSubtitles : public OMXThread
{
public:
  OMXPlayerSubtitles(const OMXPlayerSubtitles&) = delete;
  OMXPlayerSubtitles& operator=(const OMXPlayerSubtitles&) = delete;
  OMXPlayerSubtitles() noexcept;
  ~OMXPlayerSubtitles() noexcept;
  bool Open(const std::string& font_path, float font_size, OMXClock* clock) noexcept;
  void Close() noexcept;
  void Flush() noexcept;
  bool AddPacket(OMXPacket *pkt) noexcept;

private:
  struct Subtitle
  {
    double start;
    double stop;
    std::vector<std::string> text_lines;
  };

  void Process();
  void RenderLoop(const std::string& font_path, float font_size, OMXClock* clock);
  std::vector<std::string> GetTextLines(OMXPacket *pkt);

#ifndef NDEBUG
  bool m_open;
#endif

  COMXOverlayCodecText                   m_subtitle_codec;
  folly::ProducerConsumerQueue<Subtitle> m_subtitle_queue;
  std::atomic<bool>                      m_thread_stopped;
  std::atomic<bool>                      m_flush;
  std::string                            m_font_path;
  float                                  m_font_size;
  OMXClock*                              m_av_clock;
};
