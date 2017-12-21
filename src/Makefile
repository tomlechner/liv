## liv/Makefile


#LAXDIR is the location of Laxkit headers

LAXDIR=./laxkit/lax
LAXIDIR=$(LAXDIR)/interfaces



#------------------ you shouldn't have to change anything below
LD=g++
LDFLAGS= -L/usr/X11R6/lib -lXi -lXext -lX11 -lm -lpng `imlib2-config --libs` `freetype-config --libs`\
          `cups-config --libs` -lXft -lcairo -lsqlite3 -lcrypto -lfontconfig -lpthread -L$(LAXIDIR) -L$(LAXDIR)
DEBUGFLAGS= -g -Wall
CPPFLAGS= $(DEBUGFLAGS) -I$(LAXDIR)/.. `freetype-config --cflags`


objs= \
	livwindow.o 
	
liv: lax $(objs)
	g++ liv.cc $(CPPFLAGS) $(LDFLAGS) $(objs) -llaxkit -llo -o $@

liv-tuio: lax laxtuio.o $(objs)
	g++ liv.cc $(CPPFLAGS) $(LDFLAGS) $(objs) -llaxkit -llo laxtuio.o -o $@

liv-old: lax laxinterface $(objs)
	$(LD) $(LDFLAGS) $(objs) -llaxinterfaces -llaxkit -o $@

laxtuio.o:
	g++ $(CPPFLAGS) $(LAXDIR)/laxtuio.cc -c -o laxtuio.o

lax:
	cd $(LAXDIR) && $(MAKE)

laxinterface:
	cd $(LAXDIR)/interfaces && $(MAKE)


lax-old:
	cd $(LAXDIROLD) && $(MAKE)


depends:
	makedepend -fmakedepend -I$(LAXDIR)/.. -Y *.cc

include makedepend




.PHONY: clean lax laxinterface
clean:
	rm liv *.o

