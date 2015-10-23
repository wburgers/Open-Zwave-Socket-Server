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

INCLUDES	:= -I /usr/local/include/openzwave

%.o : %.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -Wno-unknown-pragmas -o $@ $<

all: openzwave-server

Sunrise:
	$(LD) $(CFLAGS) Sunrise.cpp -o Sunrise.o -lm
	
Configuration:
	$(LD) $(CFLAGS) Configuration.cpp -o Configuration.o

openzwave-server: Main.o Sunrise Configuration
	$(LD) -o $@ $(LDFLAGS) $< Sunrise.o Configuration.o -lopenzwave -lwebsockets -pthread -ludev -ljsoncpp -lsocket++

clean:
	rm -f openzwave-server *.o

XMLLINT := $(shell whereis -b xmllint | cut -c10-)

ifeq ($(XMLLINT),)
xmltest:        $(XMLLINT)
	$(error xmllint command not found.)
else
xmltest:        $(XMLLINT)
	@$(XMLLINT) --noout --schema ../../../../config/zwcfg.xsd zwcfg_*.xml
	@$(XMLLINT) --noout --schema ../../../../config/zwscene.xsd zwscene.xml
endif
