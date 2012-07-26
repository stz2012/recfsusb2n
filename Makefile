# options
B25	= -DB25

ifdef B25
  B25_PATH = ./arib25
  B25_OBJS = B25Decoder.o
  B25_OBJS_EXT = $(B25_PATH)/arib_std_b25.o $(B25_PATH)/b_cas_card.o $(B25_PATH)/multi2.o $(B25_PATH)/ts_section_parser.o
endif

DIST     = .
CC       = gcc
CFLAGS   = -O2 -march=native -g -Wall -pthread -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 $(B25)
CXX      = g++
CXXFLAGS = -O2 -march=native -g -Wall -pthread -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 $(B25)
LDFLAGS  =

OBJS     = fsusb2n.o usbops.o em2874-core.o ktv.o IoThread.o tssplitter_lite.o $(B25_OBJS)
LIBS     = -lpthread -lboost_thread-mt -lboost_filesystem
TARGET   = $(DIST)/recfsusb2n
DEPEND   = Makefile.dep

all: $(TARGET)

clean:
	rm -f $(OBJS) $(B25_OBJS_EXT) $(TARGET) $(DEPEND)

$(TARGET): $(OBJS) $(B25_OBJS_EXT)
	$(CXX) -o $(TARGET) $(OBJS) $(B25_OBJS_EXT) $(LIBS)

$(DEPEND):
	$(CC) -MM $(OBJS:.o=.cpp) > Makefile.dep

-include $(DEPEND)
