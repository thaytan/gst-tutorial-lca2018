CFLAGS=$(shell pkg-config --cflags gstreamer-1.0 gstreamer-plugins-base-1.0 gstreamer-rtsp-server-1.0)
LDFLAGS=$(shell pkg-config --libs gstreamer-1.0 gstreamer-plugins-base-1.0 gstreamer-rtsp-server-1.0)

all: playback test-rtsp-uri network-clocks

playback: playback.c
		$(CC) -o playback playback.c $(CFLAGS) $(LDFLAGS)

test-rtsp-uri: test-rtsp-uri.c
		$(CC) -o test-rtsp-uri test-rtsp-uri.c $(CFLAGS) $(LDFLAGS)

network-clocks:
	  make -C network-clocks

.PHONY: network-clocks
