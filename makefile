include config.mk

INPUT_DEMOSAUCE = demosauce.o avsource.o $(BASSOURCE) convert.o effects.o logror.o settings.o shoutcast.o sockets.o
LINK_DEMOSAUCE = $(shell pkg-config --libs shout samplerate) $(shell icu-config --ldflags-libsonly) $(LINK_FFMPEG) $(LINK_BASS)

INPUT_SCAN = scan.o avsource.o $(BASSOURCE) convert.o logror.o
LINK_SCAN = $(shell pkg-config --libs samplerate) $(LINK_FFMPEG) $(LINK_BASS) libreplaygain/libreplaygain.a

all: clean demosauce scan
	
demosauce: $(INPUT_DEMOSAUCE)
	$(CC) $(LDFLAGS) $(INPUT_DEMOSAUCE) $(LINK_DEMOSAUCE) -o demosauce

scan: $(INPUT_SCAN) 
	$(CC) $(LDFLAGS) $(INPUT_SCAN) $(LINK_SCAN) -o scan

%.o: src/%.c
	$(CC) -Wall $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f demosauce scan
	rm -f *.o
