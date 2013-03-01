include config.mk

INPUT_DEMOSAUCE = demosauce.o avsource.o $(BASSOURCE) convert.o effects.o logror.o settings.o shoutcast.o sockets.o
LINK_DEMOSAUCE = $(shell pkg-config --libs shout samplerate icu-i18n) $(LINK_FFMPEG) $(LINK_BASS) -lboost_system$(MT) -lboost_filesystem$(MT) -lboost_program_options$(MT) -lboost_thread$(MT)

INPUT_SCAN = scan.o avsource.o $(BASSOURCE) effects.o logror.o
LINK_SCAN = $(shell pkg-config --libs samplerate) $(LINK_FFMPEG) $(LINK_BASS) -lboost_system$(MT) -lboost_filesystem$(MT) libreplaygain/libreplaygain.a

all: demosauce scan
	
demosauce: $(INPUT_DEMOSAUCE)
	$(CXX) $(INPUT_DEMOSAUCE) $(LINK_DEMOSAUCE) -o demosauce

scan: $(INPUT_SCAN) 
	$(CXX) $(INPUT_SCAN) $(LINK_SCAN) -o scan

%.o: src/%.c
	$(CC) -Wall $(CFLAGS) $(CPPFLAGS) -c $< -o $@

%.o: src/%.cpp
	$(CXX) -Wall $(CFLAGS) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f demosauce scan
	rm -f *.o
