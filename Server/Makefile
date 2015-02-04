#
# Makefile for OpenzWave Mac OS X applications
# Greg Satz

# GNU make only

# requires libudev-dev

.SUFFIXES:      .cpp .o .a .s

CC     := $(CROSS_COMPILE)gcc
CXX    := $(CROSS_COMPILE)g++
LD     := $(CROSS_COMPILE)g++
AR     := $(CROSS_COMPILE)ar rc
RANLIB := $(CROSS_COMPILE)ranlib

DEBUG_CFLAGS    := -Wall -Wno-format -g -DDEBUG
RELEASE_CFLAGS  := -Wall -Wno-unknown-pragmas -Wno-format -O3
DEBUG_LDFLAGS   := -g

# Change for DEBUG or RELEASE
CFLAGS  := -c $(DEBUG_CFLAGS)
LDFLAGS := $(DEBUG_LDFLAGS)

INCLUDES	:= -I ../../src -I ../../src/command_classes/ -I ../../src/value_classes/ \
        	-I ../../src/platform/ -I ../../h/platform/unix -I ../../tinyxml/ \
		-I ../../hidapi/hidapi/

LIBS = $(wildcard ../../lib/linux/*.a) $(wildcard ../../lib/*.a) $(wildcard ../../build/*.a)

%.o : %.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -o $@ $<

all: openzwave-server

lib:
	$(MAKE) -C ../../build -$(MAKEFLAGS)

Socket: Socket.o
	$(LD) $(CFLAGS) Socket.cpp -o Socket.o

Sunrise: sunrise.o
	$(LD) -c -Wall sunrise.cpp -o Sunrise.o -lm
	
Configuration:
	$(LD) $(CFLAGS) Configuration.cpp -o Configuration.o

openzwave-server: Main.o Socket Sunrise Configuration lib
	$(LD) -o $@ $(LDFLAGS) $< $(LIBS) Socket.o Sunrise.o Configuration.o -lwebsockets -pthread -ludev

clean:
	rm -f openzwave-server *.o
	cd ../../build/; make clean

XMLLINT := $(shell whereis -b xmllint | cut -c10-)

ifeq ($(XMLLINT),)
xmltest:        $(XMLLINT)
	$(error xmllint command not found.)
else
xmltest:        $(XMLLINT)
	@$(XMLLINT) --noout --schema ../../../../config/zwcfg.xsd zwcfg_*.xml
	@$(XMLLINT) --noout --schema ../../../../config/zwscene.xsd zwscene.xml
endif
