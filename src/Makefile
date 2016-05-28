## liv/Makefile


#LAXDIR is the location of Laxkit headers

LAXDIR=./laxkit/lax
LAXIDIR=$(LAXDIR)/interfaces



#------------------ you shouldn't have to change anything below
LD=g++
LDFLAGS= -L/usr/X11R6/lib -lX11 -lm -lcairo `imlib2-config --libs` `freetype-config --libs` $(OPTIONALLIBS) -lfontconfig -lXext -lXi -lXft -lssl -L$(LAXDIR) -L$(LAXIDIR)
DEBUGFLAGS= -g -Wall
CPPFLAGS= $(DEBUGFLAGS) -I$(LAXDIR)/.. `freetype-config --cflags`


objs= \
	livwindow.o 
	
liv: lax laxtuio.o $(objs)
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

