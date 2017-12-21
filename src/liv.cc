//-------------------------------- liv.cc --------------------------------
// $Id: liv.cc 635 2012-03-12 00:21:48Z tomlechner $
// a somewhat simple Laxkit based image viewer


/*! \mainpage
 * \section main  Laxkit Image Viewer
 *
 * This is a simple, minimal image viewer written with the Laxkit.
 * You can currently view anything that Laxkit can view.
 *
 * Learn more about the Laxkit at https://github.com/Laidout/laxkit.
 *
 *
 * \todo
 * <pre>
 *
 *  main view:
 *   
 *  Main Collection    Sets:         Filesystem
 *                     [selected]    [bookmarks]
 *                     [selected2]   [ / ][ home ][opening dir]
 *                     [all files]
 *
 *
 *  freedesktop thumbnail generation
 *  file move, updating thumbnail location?
 *  finish implement thumb_location: memory|freedesktop|localdir
 *
 *  autohide mouse after some number of seconds
 *  force rotate
 *  image sets:
 *     set has name, description, creation date, file list
 *     detect from file
 *     should also understand Laidout images list files
 *  drag n drop, understand "file:" 
 *  arrange list, and dump out to console or to file
 *  sorting:        taken|format|
 *  		        exif:Flash|none
 *  sort by many:  date,name
 *    by name
 *    by tags
 *    by file date
 *    by size: file size, width, height, area
 *    by exif date taken
 *    by other exif
 *    by format
 *    by how listed on command line (default)
 *    reverse the list
 *  cache adjacent images
 *  batch rename
 *  mark by adding tags "mark1" "mark2", then select mark from tag cloud
 *  thumbnail view, browse mode, bubble zoom?
 *  load new files
 *  more than one limbo groups:
 *     images
 *     unloadable as images
 *     removed
 *     marked images
 *  for meta: run X for format Y
 *    exiv2 or exif for jpegs
 *    tiffinfo for tiffs
 *  control window geometry with command line option, including full screen
 *  figure out a decent maximize scheme
 *  flickr upload?
 *  fix timer bug in Laxkit, timer fires, but doesn't refresh
 *  intelligent large image refreshing, only paint what is necessary
 *    viewing large images is very inefficient.. speedups are certainly possible
 *
 *
 *  DONE  sort by: name|date|size|width|height|pixels|random
 *  DONE  slide show
 *  DONE  remove existing files from list, or send to limbo (marking)
 * </pre>
 */


#include <lax/anxapp.h>
#include <lax/strmanip.h>
#include <lax/attributes.h>
#include <lax/laxoptions.h>
#include <lax/language.h>

#include "livwindow.h"
//#include "laxtuio.h"



#include <cstring>
#include <iostream>
//#include <libexif/exif.h???>

#define DBG 
using namespace std;
using namespace Laxkit;
using namespace LaxFiles;
using namespace Liv;






//--------------------------------- help(), version() ----------------------------

LaxOptions options;

const char *version()
{
	return _("liv, Laxkit Image Viewer, version 0.1\n"
			 "written about 2010, 2012, and 2016 by Tom Lechner, tomlechner.com");
}

//------------------------------ main --------------------------------

int main(int argc,char **argv)
{
	srandom(times(NULL));

	options.HelpHeader(version());
	options.UsageLine("liv  [options] [files]");
	options.Add("real-size", '1', 0, "Initially show all images at 1:1 size");
	options.Add("recursive", 'r', 0, "Grab images from subdirectories too (unimplemented)");
	options.Add("bg-color",  'b', 1, "Background color, 0..255 per channel. Or gray, white, black.", 0, "'r,g,b'" );
	options.Add("in-window", 'w', 0, "Open in a window, rather than fullscreen");
	options.Add("sort",      's', 1, "Sort by one of: date,name,pixels,width,height,size,random",    0, "(sorttype)");
	options.Add("reverse",   'R', 0, "Reverse the order (after any sorting)");
	options.Add("collection",'C', 1, "Load in a collection of images");
	options.Add("slide-show",'D', 1, "Display as slideshow, with delay that many seconds",           0, "1.5");
	//options.Add("tuio",      'T', 0, "Set up a tuio listener on port 3333");
	options.Add("memthumb",  'M', 0, "Do not generate ~/.thumbnails/*, use in memory previews instead");
	options.Add("localthumb",'L', 0, "Do not generate ~/.thumbnails/*, use (filedir)/.thumbnails/*");
	options.Add("verbose",   'V', 0, "Say what a click will do as the mouse moves around");
	options.Add("version",   'v', 0, "Print out version of the program and exit");
	options.Add("help",      'h', 0, "Print out this help and exit");

	anXApp app;
	app.SetMaxTimeout(1./30 *1000000);
	app.init(argc,argv);

	////--------------------mem test
	//pid_t pid=getpid();
	//char blah[100];
	//sprintf(blah,"more /proc/%d/status",pid);
	//system(blah);
	//sprintf(blah,"more /proc/meminfo");
	//system(blah);
	//---------------------------------

	int c,index;
	int inwindow=0;
	int onetoone=LIVZOOM_Scale_To_Screen;
	char *sort=NULL;
	int reverse=0;
	//int tuio=0;
	int verbose=0;
	int usememorythumbs = LivFlags::LIV_Freedesktop_Thumbs;
	int recursive=0;
	int slidedelay=0; //default, in milliseconds
	int bgr=0, bgg=0, bgb=0; //default background color
	const char *collection=NULL;

	c=options.Parse(argc,argv, &index);
	if (c==-2) {
		cerr <<"Missing parameter for "<<argv[index]<<"!!"<<endl;
		exit(0);
	}
	if (c==-1) {
		cerr <<"Unknown option "<<argv[index]<<"!!"<<endl;
		exit(0);
	}

	LaxOption *o;
	for (o=options.start(); o; o=options.next()) {
		switch(o->chr()) {
			case 'h': options.Help(stdout); exit(0);  // Show usage summary, then exit
			case 'v': cout << version(); exit(0);    // Show version info, then exit

			case 'C': collection=o->arg(); break;  //load in a collection
			case 'V': verbose = 1; break;  //turn on verbosity
			case 'M': usememorythumbs = LivFlags::LIV_Memory_Thumbs; break;  //use thumbs in memory, do not generate any
			case 'L': usememorythumbs = LivFlags::LIV_Local_Thumbs;  break;  //generate thumbs in file's local directory
			case 'D': {
					 //slide show delay in optional arg
					if (o->arg()) slidedelay=(int) (1000*strtof(o->arg(),NULL));
					else slidedelay=2000;
					if (slidedelay<=0) {
						cerr <<"Error: Invalid value for slide show delay."<<endl;
						exit(1);
					}
					DBG cerr <<"---slide show with delay: "<<slidedelay<<" ms"<<endl;
				} break;
			case '1': onetoone=LIVZOOM_One_To_One; break;
			case 'r': recursive=1; break;
			case 'R': reverse=1; break;
			//case 'T': tuio=1; break;
			case 's': { //sort
					sort=newstr(o->arg());
			    } break;
			case 'w':
				inwindow=1;
				break;

			case 'b': 
				if (o->arg()[0]=='x' || o->arg()[0]=='#' || (o->arg()[0]=='0' && o->arg()[1]=='x')) {
					long l=strtol(o->arg()+(o->arg()[0]=='0'?2:1),NULL,16);
					bgr=(l&0xff0000)>>16;
					bgg=(l&0xff00)>>8;
					bgb=l&0xff;
				} else if (!strcasecmp(o->arg(),"white") || !strcasecmp(o->arg(),"w")) {
					bgr=bgg=bgb=255;
				} else if (!strcasecmp(o->arg(),"black") || !strcasecmp(o->arg(),"b")) {
					bgr=bgg=bgb=0;
				} else if (!strcasecmp(o->arg(),"gray") || !strcasecmp(o->arg(),"g")) {
					bgr=bgg=bgb=128;
				} else {
					int colors[4];
					int n=IntListAttribute(o->arg(),colors,4,NULL);
					if (n==3 || n==4) {
						bgr=colors[0];
						bgg=colors[1];
						bgb=colors[2];
					}
				}
				break;

		}
	}


	DBG cerr <<"v: "<<verbose<<" r:"<<recursive<<endl;

	int hh,ww,xx,yy;
	if (inwindow) {
		hh=300;
		ww=600;
		xx=yy=50;
	} else {
		xx=yy=0;
		hh=XHeightOfScreen(ScreenOfDisplay(app.dpy,DefaultScreen(app.dpy)));
		ww=XWidthOfScreen(ScreenOfDisplay(app.dpy,DefaultScreen(app.dpy)));
	}


	LivWindow *liv=new LivWindow(NULL,"Liv","Liv",
								 ANXWIN_HOVER_FOCUS,
								 //ANXWIN_HOVER_FOCUS|(inwindow?0:ANXWIN_BARE),
								 //ANXWIN_HOVER_FOCUS|ANXWIN_BARE,
								 xx,yy, ww,hh, 0,
								 onetoone,
								 !inwindow,
								 slidedelay,
								 bgr,bgg,bgb,
								 usememorythumbs);

	for (o=options.remaining(); o; o=options.next()) {
		DBG cerr <<"adding file name "<<o->arg()<<"..."<<endl;
		liv->AddFile(o->arg(),NULL,NULL,true);
		DBG cerr << "now has "<<liv->NumFiles()<<" files"<<endl;
	}

	if (collection) liv->LoadCollection(collection);
	if (!liv->NumFiles()) {
		 //use current directory when no file arguments given
		liv->AddFile(".",NULL,NULL,true);
		DBG cerr << "now has "<< liv->NumFiles() <<" files"<<endl;
	}

	if (sort) liv->Sort(sort);
	if (reverse) liv->ReverseOrder();



	app.addwindow(liv);

	

	
	
	//if (tuio) SetupTUIOListener("3333");
	app.run();

	DBG cerr <<"---------App Close--------------"<<endl;
	app.close();

	DBG cerr <<"---------Bye!--------------"<<endl;
	return 0;
}


