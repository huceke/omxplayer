#pragma once
/*
 *      Copyright (C) 2005-2011 Team XBMC
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
#endif
#include "DynamicDll.h"
#include "DllAvCore.h"
#include "DllAvCodec.h"
#include "utils/log.h"

#ifndef AV_NOWARN_DEPRECATED
#define AV_NOWARN_DEPRECATED
#endif

extern "C" {
#ifndef HAVE_MMX
#define HAVE_MMX
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#ifndef __GNUC__
#pragma warning(disable:4244)
#endif

#if (defined USE_EXTERNAL_FFMPEG)
  #if (defined HAVE_LIBAVFILTER_AVFILTER_H)
    #include <libavfilter/avfiltergraph.h>
    #include <libavfilter/vsrc_buffer.h>
  #elif (defined HAVE_FFMPEG_AVFILTER_H)
    #include <ffmpeg/avfiltergraph.h>
  #endif
  /* for av_vsrc_buffer_add_frame */
  #if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,15,0)	// could be < 15
    #include <libavcodec/avcodec.h>
  #elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,8,0)
    #include <libavfilter/avcodec.h>
  #elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,7,0)
    int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter,
                                 AVFrame *frame);
  #elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53,3,0)
    int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter,
                                 AVFrame *frame, int64_t pts);
  #else
    int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter,
          AVFrame *frame, int64_t pts, AVRational pixel_aspect);
  #endif
#else
  #include "libavfilter/avfiltergraph.h"
#endif
}

//#include "threads/SingleLock.h"

class DllAvFilterInterface
{
public:
  virtual ~DllAvFilterInterface() {}
  virtual int avfilter_open(AVFilterContext **filter_ctx, AVFilter *filter, const char *inst_name)=0;
  virtual void avfilter_free(AVFilterContext *filter)=0;
  virtual void avfilter_graph_free(AVFilterGraph **graph)=0;
  virtual int avfilter_graph_create_filter(AVFilterContext **filt_ctx, AVFilter *filt, const char *name, const char *args, void *opaque, AVFilterGraph *graph_ctx)=0;
  virtual AVFilter *avfilter_get_by_name(const char *name)=0;
  virtual AVFilterGraph *avfilter_graph_alloc(void)=0;
  virtual AVFilterInOut *avfilter_inout_alloc()=0;
  virtual void avfilter_inout_free(AVFilterInOut **inout)=0;
  virtual int avfilter_graph_parse(AVFilterGraph *graph, const char *filters, AVFilterInOut **inputs, AVFilterInOut **outputs, void *log_ctx)=0;
  virtual int avfilter_graph_config(AVFilterGraph *graphctx, void *log_ctx)=0;
  virtual int avfilter_poll_frame(AVFilterLink *link)=0;
  virtual int avfilter_request_frame(AVFilterLink *link)=0;
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,13,0)
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame, int flags)=0;
#elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,7,0)
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame)=0;
#elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53,3,0)
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame, int64_t pts)=0;
#else
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame, int64_t pts, AVRational pixel_aspect)=0;
#endif
  virtual AVFilterBufferRef *avfilter_get_video_buffer(AVFilterLink *link, int perms, int w, int h)=0;
  virtual void avfilter_unref_buffer(AVFilterBufferRef *ref)=0;
  virtual int avfilter_link(AVFilterContext *src, unsigned srcpad, AVFilterContext *dst, unsigned dstpad)=0;
};

#if (defined USE_EXTERNAL_FFMPEG)
// Use direct mapping
class DllAvFilter : public DllDynamic, DllAvFilterInterface
{
public:
  virtual ~DllAvFilter() {}
  virtual int avfilter_open(AVFilterContext **filter_ctx, AVFilter *filter, const char *inst_name)
  {
    return ::avfilter_open(filter_ctx, filter, inst_name);
  }
  virtual void avfilter_free(AVFilterContext *filter)
  {
    ::avfilter_free(filter);
  }
  virtual void avfilter_graph_free(AVFilterGraph **graph)
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(1,76,0)
    ::avfilter_graph_free(graph);
#else
    ::avfilter_graph_free(*graph);
    *graph = NULL;
#endif
  }
  void avfilter_register_all()
  {
    ::avfilter_register_all();
  }
  virtual int avfilter_graph_create_filter(AVFilterContext **filt_ctx, AVFilter *filt, const char *name, const char *args, void *opaque, AVFilterGraph *graph_ctx) { return ::avfilter_graph_create_filter(filt_ctx, filt, name, args, opaque, graph_ctx); }
  virtual AVFilter *avfilter_get_by_name(const char *name) { return ::avfilter_get_by_name(name); }
  virtual AVFilterGraph *avfilter_graph_alloc() { return ::avfilter_graph_alloc(); }
  virtual AVFilterInOut *avfilter_inout_alloc()
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,17,0)
    return ::avfilter_inout_alloc();
#else
    return (AVFilterInOut*)::av_mallocz(sizeof(AVFilterInOut));
#endif
  }
  virtual void avfilter_inout_free(AVFilterInOut **inout)
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,17,0)
    ::avfilter_inout_free(inout);
#else
    *inout = NULL;
#endif
  }
  virtual int avfilter_graph_parse(AVFilterGraph *graph, const char *filters, AVFilterInOut **inputs, AVFilterInOut **outputs, void *log_ctx)
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,16,0)
    return ::avfilter_graph_parse(graph, filters, inputs, outputs, log_ctx);
#elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,15,1)
    return ::avfilter_graph_parse(graph, filters, *inputs, *outputs, log_ctx);
#else
    return ::avfilter_graph_parse(graph, filters, *inputs, *outputs, (AVClass*)log_ctx);
#endif
  }
  virtual int avfilter_graph_config(AVFilterGraph *graphctx, void *log_ctx)
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,15,1)
    return ::avfilter_graph_config(graphctx, log_ctx);
#else
    return ::avfilter_graph_config(graphctx, (AVClass*)log_ctx);
#endif
  }
  virtual int avfilter_poll_frame(AVFilterLink *link) { return ::avfilter_poll_frame(link); }
  virtual int avfilter_request_frame(AVFilterLink *link) { return ::avfilter_request_frame(link); }
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,15,0)
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame, int64_t flags, AVRational pixel_aspect) { return ::av_vsrc_buffer_add_frame(buffer_filter, frame, flags, pixel_aspect); }
#elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,13,0)
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame, int flags) { return ::av_vsrc_buffer_add_frame(buffer_filter, frame, flags); }
#elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,7,0)
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame) { return ::av_vsrc_buffer_add_frame(buffer_filter, frame); }
#elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53,3,0)
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame, int64_t pts) { return ::av_vsrc_buffer_add_frame(buffer_filter, frame, pts); }
#else
  virtual int av_vsrc_buffer_add_frame(AVFilterContext *buffer_filter, AVFrame *frame, int64_t pts, AVRational pixel_aspect) { return ::av_vsrc_buffer_add_frame(buffer_filter, frame, pts, pixel_aspect); }
#endif
  virtual AVFilterBufferRef *avfilter_get_video_buffer(AVFilterLink *link, int perms, int w, int h) { return ::avfilter_get_video_buffer(link, perms, w, h); }
  virtual void avfilter_unref_buffer(AVFilterBufferRef *ref) { ::avfilter_unref_buffer(ref); }
  virtual int avfilter_link(AVFilterContext *src, unsigned srcpad, AVFilterContext *dst, unsigned dstpad) { return ::avfilter_link(src, srcpad, dst, dstpad); }
  // DLL faking.
  virtual bool ResolveExports() { return true; }
  virtual bool Load() {
    CLog::Log(LOGDEBUG, "DllAvFilter: Using libavfilter system library");
    return true;
  }
  virtual void Unload() {}
};
#else
class DllAvFilter : public DllDynamic, DllAvFilterInterface
{
  DECLARE_DLL_WRAPPER(DllAvFilter, DLL_PATH_LIBAVFILTER)

  LOAD_SYMBOLS()

  DEFINE_METHOD3(int, avfilter_open_dont_call, (AVFilterContext **p1, AVFilter *p2, const char *p3))
  DEFINE_METHOD1(void, avfilter_free_dont_call, (AVFilterContext *p1))
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(1,76,0)
  DEFINE_METHOD1(void, avfilter_graph_free_dont_call, (AVFilterGraph **p1))
#else
  DEFINE_METHOD1(void, avfilter_graph_free_dont_call, (AVFilterGraph *p1))
#endif
  DEFINE_METHOD0(void, avfilter_register_all_dont_call)
  DEFINE_METHOD6(int, avfilter_graph_create_filter, (AVFilterContext **p1, AVFilter *p2, const char *p3, const char *p4, void *p5, AVFilterGraph *p6))
  DEFINE_METHOD1(AVFilter*, avfilter_get_by_name, (const char *p1))
  DEFINE_METHOD0(AVFilterGraph*, avfilter_graph_alloc)
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,17,0)
  DEFINE_METHOD0(AVFilterInOut*, avfilter_inout_alloc_dont_call)
  DEFINE_METHOD1(void, avfilter_inout_free_dont_call, (AVFilterInOut **p1))
#endif
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,16,0)
  DEFINE_METHOD5(int, avfilter_graph_parse_dont_call, (AVFilterGraph *p1, const char *p2, AVFilterInOut **p3, AVFilterInOut **p4, void *p5))
#elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,15,1)
  DEFINE_METHOD5(int, avfilter_graph_parse_dont_call, (AVFilterGraph *p1, const char *p2, AVFilterInOut *p3, AVFilterInOut *p4, void *p5))
#else
  DEFINE_METHOD5(int, avfilter_graph_parse_dont_call, (AVFilterGraph *p1, const char *p2, AVFilterInOut *p3, AVFilterInOut *p4, AVClass *p5))
#endif
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,15,1)
  DEFINE_METHOD2(int, avfilter_graph_config_dont_call, (AVFilterGraph *p1, void *p2))
#else
  DEFINE_METHOD2(int, avfilter_graph_config_dont_call, (AVFilterGraph *p1, AVClass *p2))
#endif
#ifdef _LINUX
  DEFINE_METHOD1(int, avfilter_poll_frame, (AVFilterLink *p1))
  DEFINE_METHOD1(int, avfilter_request_frame, (AVFilterLink* p1))
#else
  DEFINE_FUNC_ALIGNED1(int, __cdecl, avfilter_poll_frame, AVFilterLink *)
  DEFINE_FUNC_ALIGNED1(int, __cdecl, avfilter_request_frame, AVFilterLink*)
#endif
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,13,0)
  DEFINE_METHOD3(int, av_vsrc_buffer_add_frame, (AVFilterContext *p1, AVFrame *p2, int p3))
#elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,7,0)
  DEFINE_METHOD2(int, av_vsrc_buffer_add_frame, (AVFilterContext *p1, AVFrame *p2))
#elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53,3,0)
  DEFINE_METHOD3(int, av_vsrc_buffer_add_frame, (AVFilterContext *p1, AVFrame *p2, int64_t p3))
#else
  DEFINE_METHOD4(int, av_vsrc_buffer_add_frame, (AVFilterContext *p1, AVFrame *p2, int64_t p3, AVRational p4))
#endif
  DEFINE_METHOD4(AVFilterBufferRef*, avfilter_get_video_buffer, (AVFilterLink *p1, int p2, int p3, int p4))
  DEFINE_METHOD1(void, avfilter_unref_buffer, (AVFilterBufferRef *p1))
  DEFINE_METHOD4(int, avfilter_link, (AVFilterContext *p1, unsigned p2, AVFilterContext *p3, unsigned p4))

  BEGIN_METHOD_RESOLVE()
    RESOLVE_METHOD_RENAME(avfilter_open, avfilter_open_dont_call)
    RESOLVE_METHOD_RENAME(avfilter_free, avfilter_free_dont_call)
    RESOLVE_METHOD_RENAME(avfilter_graph_free, avfilter_graph_free_dont_call)
    RESOLVE_METHOD_RENAME(avfilter_register_all, avfilter_register_all_dont_call)
    RESOLVE_METHOD(avfilter_graph_create_filter)
    RESOLVE_METHOD(avfilter_get_by_name)
    RESOLVE_METHOD(avfilter_graph_alloc)
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,17,0)
    RESOLVE_METHOD_RENAME(avfilter_inout_alloc, avfilter_inout_alloc_dont_call)
    RESOLVE_METHOD_RENAME(avfilter_inout_free, avfilter_inout_free_dont_call)
#endif
    RESOLVE_METHOD_RENAME(avfilter_graph_parse, avfilter_graph_parse_dont_call)
    RESOLVE_METHOD_RENAME(avfilter_graph_config, avfilter_graph_config_dont_call)
    RESOLVE_METHOD(avfilter_poll_frame)
    RESOLVE_METHOD(avfilter_request_frame)
    RESOLVE_METHOD(av_vsrc_buffer_add_frame)
    RESOLVE_METHOD(avfilter_get_video_buffer)
    RESOLVE_METHOD(avfilter_unref_buffer)
    RESOLVE_METHOD(avfilter_link)
  END_METHOD_RESOLVE()

  /* dependencies of libavfilter */
  DllAvUtil m_dllAvUtil;

public:
  int avfilter_open(AVFilterContext **filter_ctx, AVFilter *filter, const char *inst_name)
  {
    return avfilter_open_dont_call(filter_ctx, filter, inst_name);
  }
  void avfilter_free(AVFilterContext *filter)
  {
    avfilter_free_dont_call(filter);
  }
  void avfilter_graph_free(AVFilterGraph **graph)
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(1,76,0)
    avfilter_graph_free_dont_call(graph);
#else
    avfilter_graph_free_dont_call(*graph);
    m_dllAvUtil.av_freep(graph);
#endif
  }
  void avfilter_register_all()
  {
    avfilter_register_all_dont_call();
  }
  AVFilterInOut* avfilter_inout_alloc()
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,17,0)
    return avfilter_inout_alloc_dont_call();
#else
    return (AVFilterInOut*)m_dllAvUtil.av_mallocz(sizeof(AVFilterInOut));
#endif
  }
  int avfilter_graph_parse(AVFilterGraph *graph, const char *filters, AVFilterInOut **inputs, AVFilterInOut **outputs, void *log_ctx)
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,16,0)
    return avfilter_graph_parse_dont_call(graph, filters, inputs, outputs, log_ctx);
#elif LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,15,1)
    return avfilter_graph_parse_dont_call(graph, filters, *inputs, *outputs, log_ctx);
#else
    return avfilter_graph_parse_dont_call(graph, filters, *inputs, *outputs, (AVClass*)log_ctx);
#endif
  }
  void avfilter_inout_free(AVFilterInOut **inout)
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,17,0)
    avfilter_inout_free_dont_call(inout);
#else
    *inout = NULL;
#endif
  }
  int avfilter_graph_config(AVFilterGraph *graphctx, void *log_ctx)
  {
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(2,15,1)
    return avfilter_graph_config_dont_call(graphctx, log_ctx);
#else
    return avfilter_graph_config_dont_call(graphctx, (AVClass*)log_ctx);
#endif
  }
  virtual bool Load()
  {
    if (!m_dllAvUtil.Load())
      return false;
    return DllDynamic::Load();
  }
};
#endif
