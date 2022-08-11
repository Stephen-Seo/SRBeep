ifndef OBS_INCLUDE
OBS_INCLUDE = /usr/include/obs
endif
ifndef OBS_API_INCLUDE
OBS_API_INCLUDE = ./
endif
ifndef OBS_LIB
OBS_LIB = /usr/lib
endif
ifndef FFmpegPath
FFmpegPath = $(HOME)/ffmpeg_sources/ffmpeg
endif
ifndef FFmpegLib
FFmpegLib = $(HOME)/ffmpeg_build/lib
endif
ifndef SDL_INCLUDE
SDL_INCLUDE = $(HOME)/SDL2-2.0.10/include
endif
ifndef SDL_LIB
SDL_LIB = $(HOME)/SDL2-2.0.10/build
endif

DESTDIR ?=
PREFIX ?= /usr

RM = rm -f

CXX = g++
CXXFLAGS = -g -Wall -std=c++11 -fPIC -I/usr/include/SDL2

INCLUDE = -I$(OBS_INCLUDE) -I$(OBS_API_INCLUDE) -I$(FFmpegPath) -I$(SDL_INCLUDE)
LDFLAGS = -L$(OBS_LIB) -L$(FFmpegLib) -L$(SDL_LIB)
LDLIBS_LIB   = -lobs -lavcodec -lavformat -lswresample -lavutil -lSDL2 #libs for ffmpeg and SDL

LIB = SRBeep.so
LIB_OBJ = SRBeep.o
SRC = SRBeep.cpp

all: $(LIB)

$(LIB): $(LIB_OBJ)
	$(CXX) -shared $(LDFLAGS) $^ $(LDLIBS_LIB) -o $@

$(LIB_OBJ): $(SRC)
	$(CXX) -c $(CXXFLAGS) $^ $(INCLUDE) -o $@

#Install for obs-studio from PPA
.PHONY: install
install:
	install -D -m444 -t ${DESTDIR}/${PREFIX}/share/obs/obs-plugins/SRBeep/ ./resource/*.mp3
	install -D -m444 -t ${DESTDIR}/${PREFIX}/lib/obs-plugins/ $(LIB)

.PHONY: clean
clean:
	$(RM) $(LIB_OBJ) $(LIB)

#Install for selfbuilt obs-studio
#Place this folder in the obs-studio root folder (eg bah/rundir/obs-studio)
#should have three folders: bin, data and obs-plugins
#will need to change 64bit to 32bit if necessary
#.PHONY: install
#install:
#	mkdir ./data/obs-plugins/SRBeep
#	cp ./resource/*.mp3 ./data/64bit/obs-plugins/SRBeep/
#	chmod 777 ./data/obs-plugins/SRBeep/*.mp3
#	cp $(LIB) ./obs-plugins/64bit/
#	#cp ./depend/libSDL2.so /usr/lib/
#	#cp ./depend/lib* /usr/lib/
#	#may need to put:
#	#	libavcodec.so.##
#	#	libavcodec.so.##
#	#	libavformat.so.#
#	#	libswresample.so.#
#	#	libavutil.so.#
#	#	libx264.so.###
#	#	libvpx.so.#
#	#	libfdk-aac.so.#
#	#into /usr/lib/
#	#as well as:
	#	libSDL2.so
#	#OBS will throw a warning and tell you which is missing
#	#when the plugin is loaded

#.PHONY: clean
#clean:
#	$(RM) $(LIB_OBJ) $(LIB)
#	rm -r ./obs-plugins/64bit/$(LIB)
#	rm -r ./data/obs-plugins/SRBeep
#	rm /usr/lib/libSDL2.so

