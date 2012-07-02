include Makefile.include

CFLAGS+=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -DHAVE_CMAKE_CONFIG -D__VIDEOCORE4__ -U_FORTIFY_SOURCE -Wall -DHAVE_OMXLIB -DUSE_EXTERNAL_FFMPEG  -DHAVE_LIBAVCODEC_AVCODEC_H -DHAVE_LIBAVUTIL_OPT_H -DHAVE_LIBAVUTIL_MEM_H -DHAVE_LIBAVUTIL_AVUTIL_H -DHAVE_LIBAVFORMAT_AVFORMAT_H -DHAVE_LIBAVFILTER_AVFILTER_H -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DTARGET_RASPBERRY_PI -DUSE_EXTERNAL_LIBBCM_HOST -Wno-psabi -I$(SDKSTAGE)/opt/vc/include/ -I$(SDKSTAGE)/opt/vc/src/hello_pi/libs/vgfont

LDFLAGS+=-L./ -lc -lWFC -lGLESv2 -lEGL -lbcm_host -lopenmaxil -Lffmpeg_compiled/usr/local/lib/
INCLUDES+=-I./ -Ilinux -Iffmpeg_compiled/usr/local/include/

SRC=linux/XMemUtils.cpp \
		utils/log.cpp \
		DynamicDll.cpp \
		utils/PCMRemap.cpp \
		utils/RegExp.cpp \
		OMXSubtitleTagSami.cpp \
		OMXOverlayCodecText.cpp \
		BitstreamConverter.cpp \
		linux/RBP.cpp \
		OMXThread.cpp \
		OMXReader.cpp \
		OMXStreamInfo.cpp \
		OMXAudioCodecOMX.cpp \
		OMXCore.cpp \
		OMXVideo.cpp \
		OMXAudio.cpp \
		OMXClock.cpp \
		File.cpp \
		OMXPlayerVideo.cpp \
		OMXPlayerAudio.cpp \
		omxplayer.cpp \

OBJS+=$(filter %.o,$(SRC:.cpp=.o))

all: omxplayer.bin

%.o: %.cpp
	@rm -f $@ 
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@ -Wno-deprecated-declarations

list_test:
	$(CXX) -O3 -o list_test list_test.cpp

omxplayer.bin: $(OBJS)
	$(CXX) $(LDFLAGS) -o omxplayer.bin $(OBJS) $(SDKSTAGE)/opt/vc/src/hello_pi/libs/vgfont/libvgfont.a -lvchiq_arm -lvcos -lrt -lpthread -lavutil -lavcodec -lavformat -lswscale -lpcre -lfreetype
	#arm-unknown-linux-gnueabi-strip omxplayer.bin

clean:
	for i in $(OBJS); do (if test -e "$$i"; then ( rm $$i ); fi ); done
	@rm -f omxplayer.old.log omxplayer.log
	@rm -f omxplayer.bin
	@rm -rf omxplayer-dist
	@rm -f omxplayer-dist.tar.gz
	make -f Makefile.ffmpeg clean

ffmpeg:
	@rm -rf ffmpeg
	make -f Makefile.ffmpeg
	make -f Makefile.ffmpeg install

dist: omxplayer.bin
	mkdir -p omxplayer-dist/usr/lib/omxplayer
	mkdir -p omxplayer-dist/usr/bin
	mkdir -p omxplayer-dist/usr/share/doc
	cp omxplayer omxplayer.bin omxplayer-dist/usr/bin
	cp COPYING omxplayer-dist/usr/share/doc/
	cp README.md omxplayer-dist/usr/share/doc/README
	cp -a ffmpeg_compiled/usr/local/lib/*.so* omxplayer-dist/usr/lib/omxplayer/
	tar -czf omxplayer-dist.tar.gz omxplayer-dist
