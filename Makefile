.SUFFIXES : .c .o

SRCS   = $(OBJECTS:.o=.c)

GXX	   = gcc
CFLAGS = -c -Os -Wall
INC    = -I./ 

TARGET     = brcm_patchram_plus
OBJECTS    = daemonize.o brcm_patchram_plus.o
DEPENDENCY = daemonize.h

SCRIPT     = download-bluetooth-firmware.sh

all : $(TARGET)

$(TARGET) : $(OBJECTS)
		$(GXX) -static -o $(TARGET) $(OBJECTS)

.c.o :
		$(GXX) $(INC) $(CFLAGS) $<

clean :
		rm -rf $(OBJECTS) $(TARGET) core

install: $(TARGET)
	cp $(SCRIPT) /usr/local/bin/$(SCRIPT)
	mv $(TARGET) /usr/local/bin/brcm
