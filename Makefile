.SUFFIXES : .c .o

OBJECTS = daemonize.o brcm_patchram_plus.o

SRCS = $(OBJECTS:.o=.c)
DEPENDENCY = daemonize.h

GXX = gcc
CFLAGS = -c -Os -Wall
INC = -I./ 

TARGET = brcm_patchram_plus

all : brcm_patchram_plus
$(TARGET) : $(OBJECTS)
		$(GXX) -static -o $(TARGET) $(OBJECTS)

.c.o :
		$(GXX) $(INC) $(CFLAGS) $<

clean :
		rm -rf $(OBJECTS) $(TARGET) core

