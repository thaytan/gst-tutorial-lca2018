
TARGET=playback-sync
TARGET2=netclock-server

CFLAGS=-Wall -O0 -g `pkg-config --cflags gstreamer-1.0 gstreamer-net-1.0 gstreamer-pbutils-1.0`
LDFLAGS=`pkg-config --libs gstreamer-1.0 gstreamer-net-1.0 gstreamer-pbutils-1.0`

all: $(TARGET) $(TARGET2)

$(TARGET): $(TARGET).c
	gcc -o $@ $< $(CFLAGS) $(LDFLAGS)

$(TARGET2): $(TARGET2).c
	gcc -o $@ $< $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TARGET2)
