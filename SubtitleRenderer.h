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

#include <EGL/egl.h>
#include <VG/openvg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include <vector>
#include <unordered_map>
#include <string>
#include "folly/Workarounds.h"

class SubtitleRenderer {
public:
  SubtitleRenderer(const SubtitleRenderer&) = delete;
  SubtitleRenderer& operator=(const SubtitleRenderer&) = delete;
  SubtitleRenderer(int level,
                   const std::string& font_path,
                   float font_size,
                   float margin_left,
                   float margin_bottom,
                   unsigned int white_level,
                   unsigned int box_opacity);
  ~SubtitleRenderer() noexcept;

  void prepare(const std::vector<std::string>& text_lines) noexcept;

  void unprepare() noexcept {
    prepared_ = false;
  }

  void show_next() noexcept {
    if (prepared_)
      draw();
    swap_buffers();
  }

  void hide() noexcept {
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
  unsigned int white_level_;
  unsigned int box_opacity_;
};