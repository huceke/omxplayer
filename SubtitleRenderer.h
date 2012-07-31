#pragma once

// Author: Torarin Hals Bakke (2012)

// Boost Software License - Version 1.0 - August 17th, 2003

// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:

// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <EGL/egl.h>
#include <VG/openvg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include <boost/config.hpp>
#include <vector>
#include <unordered_map>
#include <string>

class SubtitleRenderer {
public:
  SubtitleRenderer(const SubtitleRenderer&) = delete;
  SubtitleRenderer& operator=(const SubtitleRenderer&) = delete;
  SubtitleRenderer(int level,
                   const std::string& font_path,
                   float font_size,
                   float margin_left,
                   float margin_bottom,
                   bool centered,
                   unsigned int white_level,
                   unsigned int box_opacity);
  ~SubtitleRenderer() BOOST_NOEXCEPT;

  void prepare(const std::vector<std::string>& text_lines) BOOST_NOEXCEPT;

  void unprepare() BOOST_NOEXCEPT {
    prepared_ = false;
  }

  void show_next() BOOST_NOEXCEPT {
    if (prepared_)
      draw();
    swap_buffers();
  }

  void hide() BOOST_NOEXCEPT {
    clear();
    swap_buffers();
    if (prepared_)
      draw();
  }

private:
  struct InternalChar {
    char32_t codepoint;
    bool italic;
  };

  struct InternalGlyph {
    int advance;
  };

  static void draw_text(VGFont font, VGFont italic_font,
                        const std::vector<InternalChar>& text,
                        int x, int y,
                        unsigned int lightness);

  void destroy();
  void initialize_fonts(const std::string& font_name, float font_size);
  void destroy_fonts();
  void initialize_egl();
  void destroy_egl();
  void initialize_window(int layer);
  void destroy_window();
  void clear();
  void draw();
  void swap_buffers();
  void prepare_glyphs(const std::vector<InternalChar>& text);
  void load_glyph(char32_t codepoint);
  int get_text_width(const std::vector<InternalChar>& text);
  std::vector<InternalChar> get_internal_chars(const std::string& str);

  bool prepared_;
  DISPMANX_ELEMENT_HANDLE_T dispman_element_;
  DISPMANX_DISPLAY_HANDLE_T dispman_display_;
  uint32_t screen_width_;
  uint32_t screen_height_;
  EGLDisplay display_;
  EGLContext context_;
  EGLSurface surface_;
  VGFont vg_font_;
  VGFont vg_font_border_;
  VGFont vg_font_italic_;
  VGFont vg_font_italic_border_;
  FT_Library ft_library_;
  FT_Face ft_face_;
  FT_Stroker ft_stroker_;
  std::unordered_map<char32_t,InternalGlyph> glyphs_;
  std::vector<std::vector<InternalChar>> internal_lines_;
  std::vector<std::pair<int,int>> line_positions_;
  std::vector<int> line_widths_;
  int line_height_;
  int box_offset_;
  int box_h_padding_;
  float margin_left_;
  float margin_bottom_;
  bool centered_;
  unsigned int white_level_;
  unsigned int box_opacity_;
};
