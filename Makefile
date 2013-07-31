#replace multi-spaces with a tap for Makefile indent
NAME = lua-mcch
VERSION = 0.1 
DIST := $(NAME)-$(VERSION)

CC = gcc 
RM = rm -rf 

#for test
CFLAGS = -Wall -g -fPIC -I/home/microwish/lua/include
#for production
#CFLAGS = -Wall -g -O2 -fPIC -I/home/microwish/lua/include
LFLAGS = -shared -L/home/microwish/lua/lib -llua
INSTALL_PATH = /home/microwish/lua-mcch/lib

all: bd_mcch.so

bd_mcch.so: lmcchlib.o mcch.o
    $(CC) -o $@ $^ $(LFLAGS)

mcch.o: mcch.c
    $(CC) -o $@ $(CFLAGS) -c $<

lmcchlib.o: lmcchlib.c
    $(CC) -o $@ $(CFLAGS) -c $<

install: bd_mcch.so
    install -D -s $< $(INSTALL_PATH)/$<

clean:
    $(RM) *.so *.o

dist:
    if [ -d $(DIST) ]; then $(RM) $(DIST); fi
    mkdir -p $(DIST)
    cp *.c Makefile $(DIST)/
    tar czvf $(DIST).tar.gz $(DIST)
    $(RM) $(DIST)

.PHONY: all clean dist
