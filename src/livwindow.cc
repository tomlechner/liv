// $Id: livwindow.cc 635 2012-03-12 00:21:48Z tomlechner $


#include <dirent.h>
#include <pthread.h>

#include "livwindow.h"

#include <lax/language.h>
#include <lax/laximlib.h>
#include <lax/laximages-imlib.h>
#include <lax/strmanip.h>
#include <lax/fileutils.h>
#include <lax/laxutils.h>
#include <lax/lineedit.h>
#include <lax/filedialog.h>

#include <lax/lists.cc>
#include <lax/refptrstack.cc>

#include <cstring>
#include <iostream>


#define DBG 
using namespace std;
using namespace Laxkit;
using namespace LaxFiles;


namespace Liv {


// *** temp global var:
Imlib_Image overlay_color;




//see PositionTagBoxes(), how many lines down from top to place tag boxes
#define SHOW_NUM_SINGLE_LINE  (4)


//------------------------------global setup------------------------

//! Path of file to run to pipe in exif info. Default is look for exiv2.
const char *exif_exec="/usr/bin/exiv2"; 



//----------------------thread info--------------------------------
int numthreads=0; //current number of image generation threads
int maxthreads=1; //max number of image generation threads
pthread_mutex_t tomakelist_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t imlib_mutex     =PTHREAD_MUTEX_INITIALIZER;


//-------------------------------- preview creation threads ----------------------------------

PtrStack<char> previews_to_make(2);
pthread_mutex_t generate_mutex=PTHREAD_MUTEX_INITIALIZER;

void generate_preview_thread();
void *actually_generate_previews(void *);


//! In another thread, create a scaled image and save to the "large" part of freedesktop thumbs.
/*! Each must fit in a 256x256 square.
 *
 * If there is "/.thumbnails/normal" or "/.thumbnails/large" in the path, then do nothing,
 * as it is already a thumbnail.
 */
void generate_preview(const char *file, const char *preview)
{
	if (strstr(file,"/.thumbnails/normal") || strstr(file,"/.thumbnails/large")) return;

	DBG cerr <<"Need to generate preview for "<<preview<<"..."<<endl;

	pthread_mutex_lock(&tomakelist_mutex);

	previews_to_make.push(newstr(file));
	previews_to_make.push(newstr(preview));

	pthread_mutex_unlock(&tomakelist_mutex);

	generate_preview_thread();
}

//! Create and wait for maxthreads number of preview creation threads.
void generate_preview_thread()
{
	//if (pthreads_mutex_trylock(&generate_mutex)==EBUSY) return; //already generating
	if (previews_to_make.n==0) return;
	if (numthreads>=maxthreads) return;

	pthread_mutex_lock(&generate_mutex);
	pthread_t gen_thread;
	if (pthread_create(&gen_thread,NULL,actually_generate_previews,NULL)==0) 
		numthreads++;

	DBG cerr <<"Thread "<<gen_thread<<" created and running in background. numthreads="<<numthreads<<endl;

	pthread_mutex_unlock(&generate_mutex);
}

//! Thread function to create previews until no more in previews_to_make.
/*! Decrements numthreads on completion.
 */
void *actually_generate_previews(void *)
{
	while (previews_to_make.n) {
		 //make the preview next in the list
		char *file,*preview;

		pthread_mutex_lock(&tomakelist_mutex);
		if (previews_to_make.n==0) { //guard against n race condition
			pthread_mutex_unlock(&tomakelist_mutex);
			break;
		}

		preview=previews_to_make.pop();
		file=previews_to_make.pop();
		pthread_mutex_unlock(&tomakelist_mutex);


		DBG cerr <<"...Generating preview in thread "<<pthread_self()<<" for "<<file<<endl;

		pthread_mutex_lock(&imlib_mutex);
		generate_preview_image(file,preview,"png",256,256,1);
		pthread_mutex_unlock(&imlib_mutex);
		delete[] file;
		delete[] preview;

		anXApp::app->bump();
	}

	pthread_mutex_lock(&generate_mutex);
	numthreads--;
	pthread_mutex_unlock(&generate_mutex);

	return NULL;
}




//------------------------------ ActionBox ----------------------------------
/*! \class ActionBox
 * \brief Describe possible screen areas to produce an action.
 */

ActionBox::ActionBox()
{
	action=LIVA_None;
	index=0;
	is_abs_dims=1;
	action_class=ACTIONCLASS_Display;
	v=0;
	show_hover=0;
	mode=0;
	submode=0;
}

ActionBox::ActionBox(const char *t,  //!< box text
					 int a,          //!< action id
					 int i,          //!< Sort of subaction of a, see ActionBox::index.
					 int isabsdims,  //!< 2 is real coords, 1 screen coords, 0 is 0..1 space
					 double x1, double x2, double y1, double y2, //!< bounds
					 int shover,     //!< Show a highlighted box when the mouse hovers over the box
					 int mde         //!< Mode this action works in
					)
		: DoubleBBox(x1,x2,y1,y2), 
		  action(a),
		  is_abs_dims(isabsdims) 
{
	index=i;
	text=t;
	v=0;
	show_hover=shover;
	mode=mde;
	submode=0;
}

int ActionBox::GetInt()
{
	return (v+.5);
}

double ActionBox::GetDouble()
{
	return v;
}



//----------------------------- class ImageThumb -------------------

/*! \class ImageThumb
 * \brief Placeholder of a thumbnail of an ImageFile in a collection, set, or directory view.
 *
 */



ImageThumb::ImageThumb()
{
	parent=NULL;
	image=NULL;
	x=y=0;
	width=height=0;
	kidx=kidy=0;
	kidswidth=kidsheight=0;
	scale_to_kids=1;
	gap=0;
}

ImageThumb::ImageThumb(ImageFile *img, int xx,int yy)
{
	parent=NULL;
	x=y=0;
	width=height=0;
	kidx=kidy=0;
	kidswidth=kidsheight=0;
	scale_to_kids=1;
	image=NULL;
	Set(img,xx,yy);
}

ImageThumb::~ImageThumb()
{
	if (image) image->dec_count();
}

int ImageThumb::Gap(int newgap)
{
	return gap=newgap;
}

int ImageThumb::Add(ImageThumb *thumb)
{
	return kids.push(thumb);
}

int ImageThumb::Add(ImageFile *img)
{
	ImageThumb *thumb=new ImageThumb(img,0,0);
	kids.push(thumb);
	thumb->dec_count();
	return 0;
}

void ImageThumb::Set(ImageFile *img, int xx,int yy)
{
	if (img!=image) {
		if (image) image->dec_count();
		image=img;
		if (image) image->inc_count();
	}
	x=xx;
	y=yy;

	if (img) {
		width =img->pwidth;
		height=img->pheight;
	} else {
		width=0;
		height=0;
	}
}

void ImageThumb::Set(int xx,int yy, int ww,int hh)
{
	x=xx;
	y=yy;
	width=ww;
	height=hh;
}


/*! If how==0, then layout using kid's dimensions as best as possible within
 * existing width and height of *this.
 * how==1 lays out in one row.
 *
 * Return 1 for *this has new dimensions, or 0 for same dimensions.
 */
void ImageThumb::Layout(int how)
{
	int maxwidth=width;
	double wholew=0,wholeh=0;

	int rowheight=0;
	int n;
	int previewsize=256;
	flatpoint curpos;
	ImageFile *img;
	Imlib_Image ii;
	//double scale=norm(flatpoint(thumb_matrix[0],thumb_matrix[1]));
	double iw,ih; //width and height of actual image
	int w,h;     //width and height of image's thumbnail
	int rowstartindex;
	rowstartindex=0;

	int thumbdisplaywidth=0;
	if (how==1) thumbdisplaywidth=10000000;
	if (thumbdisplaywidth<=0) thumbdisplaywidth=maxwidth;

	//if (how==1) {
	//}

	for (int c=0; c<kids.n; c++) {
		n++;

		img=kids.e[c]->image;
		iw=img->width;
		ih=img->height;

		w=img->pwidth;
		h=img->pheight;

		if (w<=0 || h<=0) { // need to find preview dimensions
			int previewfound=0;
			if (img->preview) {
				ii=imlib_load_image(img->preview);
				if (ii) {
					previewfound=1;
					imlib_context_set_image(ii);
					w=imlib_image_get_width();
					h=imlib_image_get_height();
					imlib_free_image();
				}
			} 
			if (!previewfound) { //no preview image found, so figure dimensions in other ways
				 //draw box with x in it for no preview
				if (iw<=0 || ih<=0) { iw=ih=100; }
				if (iw>ih) {
					w=previewsize;
					h=ih*previewsize/iw;
				} else {
					h=previewsize;
					w=iw*previewsize/ih;
				}

			}
			img->pwidth =w;
			img->pheight=h;

		} //find preview dimensions

		w+=gap;
		h+=gap;

		if (curpos.x+w<=thumbdisplaywidth || (curpos.x+w>thumbdisplaywidth && c==rowstartindex)) {
			 //update thumb pos when in current row or at beginning and image is really wide
			 //update thumb location info <- these are in screen space
			kids.e[c]->Set(curpos.x,curpos.y, w,h);
		}

		 //advance curpos to next row, or next in row
		if (curpos.x+w>thumbdisplaywidth || c==kids.n-1) {
			 //start new row
			if (curpos.x+w>wholew) wholew=curpos.x+w;

			if (c!=rowstartindex && c!=kids.n-1) c--; //do not use final image, it will start next row
			else if (h>rowheight) rowheight=h;

			wholeh+=rowheight;

			if (curpos.x+w>thumbdisplaywidth) {
				curpos.x=0;
				curpos.y+=rowheight;
			}

			rowstartindex=c+1;
		} else {
			 //append to current row
			curpos.x+=w; 
			if (h>rowheight) rowheight=h;
		}
	} //foreach image in files

	kidswidth=thumbdisplaywidth;
	kidsheight=wholeh;
	if (((double)kidsheight)/kidswidth>((double)width)/height)
		scale_to_kids=((double)height)/kidsheight;
	else scale_to_kids=((double)width)/kidswidth;
	//kidsx=(width-scale_to_kids*kidswidth)/2;
	//kidsy=(height-scale_to_kids*kidsheight)/2;

	return;
}



//----------------------------- class ImageFile -------------------

/*! \class ImageFile
 *
 * Either a set of images or an image itself.
 *
 *  \todo see libexiv2 for proper exif discovery
 */

ImageFile::ImageFile()
{
	is_set=0;
	parent=NULL;
	lastviewtime=0;
	mark=0;

	transform_identity(matrix);

	state=FILE_Not_accessed;
	filetype=FILE_is_unknown;

	filename=NULL;
	preview=NULL;
	pwidth=pheight=0;

	name=NULL;
	image=NULL;
	meta=NULL;
	title=NULL;
	description=NULL;
}

/*! Copies over fname to filename, but does not more data lookup.
 */
ImageFile::ImageFile(const char *fname)
{
	is_set=0;
	parent=NULL;
	lastviewtime=0;
	mark=0;

	transform_identity(matrix);

	state=FILE_Not_accessed;
	filetype=FILE_is_unknown;

	filename=newstr(fname);

	preview=NULL;
	pwidth=pheight=0;

	name=NULL;
	image=NULL;
	meta=NULL;
	title=NULL;
	description=NULL;

	if (filename && type==ITEM_Is_File) {
		 //find a suitable freedesktop thumbnail, if any
		preview=freedesktop_thumbnail(filename,'l');

		//DBG cerr <<"for file "<<filename<<" trying thumb "<<preview<<endl;
		if (file_exists(preview,1,NULL)!=S_IFREG) {
			 //preview file does not seem to exist, try a standard freedesktop one
			delete[] preview;
			preview=freedesktop_thumbnail(filename,'n');
			//DBG cerr <<"for file "<<filename<<" trying thumb "<<preview<<endl;
			if (file_exists(preview,1,NULL)!=S_IFREG) {
				 //freedesktop one doesn't seem to exist, maybe try again later!
				delete[] preview;
				preview=NULL;
			}
		}

		if (!preview) {
			 //no preview file found, try the freedesktop 'l', and render in background
			preview=freedesktop_thumbnail(filename,'l');
			generate_preview(filename,preview); //background render
		}


		DBG cerr <<"For file \""<<filename<<endl;
		DBG if (preview) cerr <<" -> Using preview "<<preview<<endl; else cerr <<" -> no preview found!"<<endl;

		fillinfo(FILE_Has_stat|FILE_Has_image);
	}


	//struct stat {
	//dev_t     st_dev;     /* ID of device containing file */
	//ino_t     st_ino;     /* inode number */
	//mode_t    st_mode;    /* protection */
	//nlink_t   st_nlink;   /* number of hard links */
	//uid_t     st_uid;     /* user ID of owner */
	//gid_t     st_gid;     /* group ID of owner */
	//dev_t     st_rdev;    /* device ID (if special file) */
	//off_t     st_size;    /* total size, in bytes */
	//blksize_t st_blksize; /* blocksize for filesystem I/O */
	//blkcnt_t  st_blocks;  /* number of blocks allocated */
	//time_t    st_atime;   /* time of last access */
	//time_t    st_mtime;   /* time of last modification */
	//time_t    st_ctime;   /* time of last status change */
	//};
}

ImageFile::ImageFile(int ntype, const char *nname, const char *nfilename, const char *ntitle, const char *ndesc,Attribute *nmeta)
{
	type=ntype;
	name=newstr(nname);
	filename=newstr(nfilename);
	title=newstr(ntitle);
	description=newstr(ndesc);
	meta=(nmeta ? nmeta->duplicate() : NULL);

	parent=NULL;
	lastviewtime=0;
	mark=0;

	transform_identity(matrix);

	state=FILE_Not_accessed;
	filetype=FILE_is_unknown;


	preview=NULL;
	pwidth=pheight=0;

	if (filename && type==ITEM_Is_File) {
		 //find a suitable freedesktop thumbnail, if any
		preview=freedesktop_thumbnail(filename,'l');

		//DBG cerr <<"for file "<<filename<<" trying thumb "<<preview<<endl;
		if (file_exists(preview,1,NULL)!=S_IFREG) {
			 //preview file does not seem to exist, try a standard freedesktop one
			delete[] preview;
			preview=freedesktop_thumbnail(filename,'n');
			//DBG cerr <<"for file "<<filename<<" trying thumb "<<preview<<endl;
			if (file_exists(preview,1,NULL)!=S_IFREG) {
				 //freedesktop one doesn't seem to exist, maybe try again later!
				delete[] preview;
				preview=NULL;
			}
		}

		if (!preview) {
			 //no preview file found, try the freedesktop 'l', and render in background
			preview=freedesktop_thumbnail(filename,'l');
			generate_preview(filename,preview); //background render
		}


		DBG cerr <<"For file \""<<filename<<endl;
		DBG if (preview) cerr <<" -> Using preview "<<preview<<endl; else cerr <<" -> no preview found!"<<endl;

		fillinfo(FILE_Has_stat|FILE_Has_image);
	}

}

ImageFile::~ImageFile()
{
	if (image) image->dec_count();
	if (filename) delete[] filename;
	if (name) delete[] name;
	if (preview) delete[] preview;
	if (meta) delete meta;
	if (title) delete[] title;
	if (description) delete[] description;
}

/*! which&FILE_Has_stat  means do stat, 
 *  which&FILE_Has_image means load image data
 *  which&FILE_Has_exif  means load exif (via exif_exec) info
 *
 *  Return 0 for success, nonzero for error.
 */
int ImageFile::fillinfo(int which)
{
	DBG cerr <<"getting info for "<<filename<<": "<<which<<endl;

	if ((which&FILE_Has_stat) && !(state&FILE_Has_stat)) {
		int c=stat(filename, &fileinfo);
		if (c==0) state|=FILE_Has_stat;
		else state&=~FILE_Has_stat;
	}
	if ((which&FILE_Has_image) && !(state&FILE_Has_image) && !image) {
		image=load_image(filename);
		if (!image) {
			if (!filetype || filetype==FILE_is_image) filetype=0;
			state&=~FILE_Has_image;
		} else {
			state|=FILE_Has_image;
			width=image->w();
			height=image->h();
		}
	}

	if ((which&FILE_Has_exif) && !(state&FILE_Has_exif) && !meta) {
		state|=FILE_Has_exif;
		size_t c;
		char *exif=new char[1024], *data;
		int at=0;
		sprintf(exif,"%s %s",exif_exec,filename);
		FILE *f=popen(exif,"r");
		exif[0]='\0';
		data=exif;
		if (f) {
			while (1) {
				c=fread(data,1,1024,f);
				if (c<1024) {
					data[c]='\0';
					break;
				}
				 //expand exif
				char *temp=exif;
				at+=1024;
				exif=new char[at+1024];
				memcpy(exif,temp,at);
				delete[] temp;
				data=exif+at;
			}
			fclose(f);
			if (!meta) meta=new Attribute;
			meta->push("exif",exif);
		}
		
		DBG cout <<" ~~~ scanned in exif:"<<endl<<exif;
	}

	return 0;
}

//------------------------------ LivWindow ----------------------------------------


//! Return the number of SHOW_*, for single line items.
static int numShowTypes(int i)
{
	int n=0;
	if (i&SHOW_Filename) n++;
    if (i&SHOW_Index   ) n++;
    if (i&SHOW_Filesize) n++;
    if (i&SHOW_Dims    ) n++;

	return n;
}

/*! \class LivWindow
 * \brief The main window.
 */
/*! \var int LivWindow::showbasics
 * \brief What meta info to show.
 *
 * Currently:
 *   filename,
 *   count in set,
 *   image file size,
 *   image dimensions,
 *   tags
 */


/*! If slided>0, then show as slideshow, each slide slided milliseconds.
 */
LivWindow::LivWindow(anXWindow *parnt,const char *nname,const char *ntitle,unsigned long nstyle,
				     int xx,int yy,int ww,int hh,int brder,
					 int zoom,int fs,int slided,
					 int bgr, int bgg, int bgb, //default background color
					 int memorythumbs
					)
	: anXWindow(parnt,nname,ntitle,nstyle|ANXWIN_REMEMBER|ANXWIN_DOUBLEBUFFER,
				xx,yy,ww,hh,brder,
				NULL,0,NULL)
{
	 //init image in memory stuff, and top level boxes
	current=NULL;
	current_image_index=-1; //index in current set
	collectionfile=NULL;
	InitializePlacements();

	 //init gui things
	InitActions();
	actions=&imageactions;

	curzone=&collection;
	transform_identity(screen_matrix);
	transform_identity(thumb_matrix);
	thumbdisplaywidth=-1;
	previewsize=256;
	screen_rotation=0;

	currentactionbox=NULL;
	device1=device2=0;
	hover_image=-1;
	hover_text=NULL;

	win_colors=new WindowColors;
	win_colors->bg=rgbcolor(bgr,bgg,bgb);
	win_colors->fg=rgbcolor(bgr<128?255:0, bgg<128?255:0, bgb<128?255:0);

	 //viewing state:
	livflags=0;// LIV_Autoremove
	timerid=0;
	fullscreen=fs;
	showoverlay=0;
	showmarkedpanel=1;
	imagesonly=1; //images, text files, other files, directories
	dirsets=1; //0 is load dir contents and not keep as a set, 1 load as set
	isonetoone=0;
	firsttime=1;
	showbasics=SHOW_All;
	showmeta=0;
	zoommode=zoom; //1==scale to screen, 2==scale to screen if bigger, 0==exact size
	verbose=0;
	currentmark=1;
	viewmarked=0;
	lastviewjump=0;//whether zoomed into an image (0), or just jumped right to it (1)

	slidedelay=slided;//in milliseconds
	if (slidedelay>0) viewmode=VIEW_Slideshow;
	else viewmode=VIEW_Normal;
	lastmode=viewmode;

	//overlaymodifier=imlib_create_color_modifier();
	overlay_color=NULL;
	overlayalpha=50; //default overlay transparency

	first_tag_action=0;

	needtomap=1;
	thumbgap=5;

	thumb_location=memorythumbs; //fd, memory, or local
}

LivWindow::~LivWindow()
{
	if (hover_text) delete[] hover_text;
	if (collectionfile) delete[] collectionfile;
	if (sc) sc->dec_count();
}

//! Set up placement of main 3 areas, which are direct children of top.
void LivWindow::InitializePlacements()
{
	app->ScreenInfo(0,&defaultwidth,&defaultheight,NULL,NULL,NULL,NULL,NULL,NULL);

	selection.width =defaultwidth;
	selection.height=defaultheight;

	collection.width =defaultwidth;
	collection.height=defaultheight;

	filesystem.width =defaultwidth;
	filesystem.height=defaultheight;

	top.Gap(defaultwidth*.1);
	top.Add(&selection);
	top.Add(&collection);
	top.Add(&filesystem);
	top.Layout(1);//layout full in one row
}

int LivWindow::init()
{
	if (fullscreen) {
		 //set up window state to cooperate with full screen (does this work???)
		Atom prop_fs = XInternAtom(app->dpy, "_NET_WM_STATE_FULLSCREEN", False);
		Atom prop_state = XInternAtom(app->dpy, "_NET_WM_STATE", False);

		XEvent ev;
		memset(&ev, 0, sizeof(ev));
		ev.xclient.type = ClientMessage;
		ev.xclient.message_type = prop_state;
		ev.xclient.display = app->dpy;
		ev.xclient.window = xlib_window;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = (fullscreen ? 1 : 0);
		ev.xclient.data.l[1] = prop_fs;

		XChangeProperty(app->dpy, xlib_window, prop_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&prop_fs, 1);
	}

	PositionMiscBoxes();
	return 0;
}

int LivWindow::Idle(int tid)
{
	SelectImage(current_image_index+1);
	needtodraw=1;
	return 0;
}

//! Position the thumbnails in thumb space.
/*! Returns 1 for images positioned.
 */
int LivWindow::MapThumbs()
{
	curzone->width=win_w;
	ImageThumb *zone=curzone;
	//int changed;

	do {
		//changed=
		zone->Layout(0);
		//if (!changed) break;
		zone=zone->parent;
	} while (zone);

	 //make sure new setup is actually on screen
	// ***
//	if (needtomap==2) {
//		***
//		p[0]=transform_point(thumb_matrix,0,0); //upper left
//		p[1]=transform_point(thumb_matrix,flatpoint(wholew,wholeh)); //lower right
//
//		if (p[1].x<0) thumb_matrix[4]-=p[0].x;
//		else if (p[0].x>win_w) thumb_matrix[4]-=(p[0].x-win_w);
//		if (p[1].y<0) thumb_matrix[5]-=p[0].y;
//		else if (p[0].y>win_h) thumb_matrix[5]-=(p[0].y-win_h);
//	}

	needtomap=0;

	return 1;
}

//! Change view mode.
/*! Returns old mode.
 */
int LivWindow::Mode(int newmode)
{
	int oldmode=viewmode;
	viewmode=newmode;
	menuactions.flush();
	needtodraw=1;

	return oldmode;
}

int LivWindow::StartSlideshow() 
{
	Mode(VIEW_Slideshow);
	if (slidedelay<=0) slidedelay=2000; //in milliseconds
	timerid=app->addtimer(this,slidedelay,slidedelay,-1);
	needtodraw=1;
	return 0;
}

void LivWindow::Refresh()
{
	needtodraw=0;
	
	if (firsttime) {
		if (curzone->kids.n==0) {
			DBG cerr << "No more files, so quitting!"<<endl;
			app->destroywindow(this);
			return;
		}

		if (VIEW_Slideshow && slidedelay>0) StartSlideshow();
		app->setfocus(this);
		//SetupBackBuffer();
		RotateScreen(screen_rotation);
		MapThumbs();
		setzoom();
		firsttime=0;
	}

	Drawable xdrawable=xlibDrawable();
	laximlib_alternate_drawable(xdrawable);

	//clear window
	drawing_function(LAXOP_Over);
	foreground_color(win_colors->bg);
	fill_rectangle(this, 0,0,win_w,win_h);
	foreground_color(win_colors->fg);

	imlib_context_set_drawable(xdrawable);

	pthread_mutex_lock(&imlib_mutex);
	if (viewmode==VIEW_Help) RefreshHelp(); 
	if (viewmode==VIEW_Normal) RefreshNormal(); 
	if (viewmode==VIEW_Thumbs) RefreshThumbs(); 
	SwapBuffers();
	pthread_mutex_unlock(&imlib_mutex);

}

/*! Screen refresh for VIEW_Help mode.
 */
void LivWindow::RefreshHelp()
{
	ShortcutManager *m=GetDefaultShortcutManager();

	ShortcutDefs *s;
	WindowActions *a;
	WindowAction *aa;
	char buffer[100],str[400];
	char *helpkeys=NULL;

	for (int c=0; c<m->shortcuts.n; c++) {
	    sprintf(str,"%s:\n",m->shortcuts.e[c]->area);
		appendstr(helpkeys,str);

	    s=m->shortcuts.e[c]->Shortcuts();
	    a=m->shortcuts.e[c]->Actions();
	
	     //output all bound keys
	    if (s) {
	        for (int c2=0; c2<s->n; c2++) {
	            sprintf(str,"  %-15s ",m->ShortcutString(s->e[c2], buffer));
	            if (a) aa=a->FindAction(s->e[c2]->action); else aa=NULL;
	            if (aa) {
	                 //print out string id and commented out description
	                //sprintf(str+strlen(str),"%-20s",aa->name);
	                if (!isblank(aa->description)) sprintf(str+strlen(str),"%s",aa->description);
	                sprintf(str+strlen(str),"\n");
	            } else sprintf(str+strlen(str),"%d\n",s->e[c2]->action); //print out number only
				appendstr(helpkeys,str);
	        }
	    }
		appendstr(helpkeys,"\n");
	}

	textout_multiline(this, helpkeys,-1, win_w/4,win_h/2, LAX_LEFT|LAX_VCENTER);
	delete[] helpkeys;
}

/*! Screen refresh for VIEW_Thumbs mode.
 */
void LivWindow::RefreshThumbs()
{
	//int x,y,w,h;
	flatpoint curpos;
	//flatpoint p[4];
	//ImageFile *img;
	//ImageThumb *imgt;
	//double scale=norm(flatpoint(thumb_matrix[0],thumb_matrix[1]));
	//double ww,hh;
	//double iw,ih;
	int n=0;

	if (thumbdisplaywidth<=0) thumbdisplaywidth=win_w;
	if (needtomap) MapThumbs();

	DrawThumbsRecurseUp(curzone);
	DrawThumbsRecurseDown(curzone);


	if (n==0) {
		foreground_color(win_colors->fg);
		textout(this,_("No Images"),-1, win_w/2,win_h/2, LAX_CENTER);
	}

	 //hover a message near image that mouse is currently over
	if (hover_image>=0 && hover_image<curzone->kids.n) {
		foreground_color(win_colors->bg);
		fill_rectangle(this, hover_area.minx,hover_area.miny, hover_area.maxx-hover_area.minx,hover_area.maxy-hover_area.miny);
		foreground_color(win_colors->fg);
		textout(this, hover_text,-1, (hover_area.minx+hover_area.maxx)/2,(hover_area.miny+hover_area.maxy)/2, LAX_CENTER); 
	}
	
//		 // *** show all tags
//		if (tagcloud.NumberOfTags()) {
//			int n=tagcloud.NumberOfTags();
//			int x=0, y=0, ystart=0;
//			int textheight=app->defaultlaxfont->textheight();
//			int colstart=0;
//			int maxw=0,w;
//
//			foreground_color(win_colors->fg);
//			for (int c=0; c<n; c++) {
//				w=textout(this,tagcloud.GetTag(c),-1, x,y, LAX_LEFT|LAX_TOP);
//				if (w>maxw) maxw=w;
//				y+=textheight;
//				if (y+textheight>win_h && c>colstart) {
//					x+=maxw;
//					ystart=0;
//					colstart=c+1;
//				}
//			}
//		}


	 //--- draw action boxes

	//for (int c=0; c<actions->n; c++) {
	//	if (actions->e[c]->mode && actions->e[c]->mode!=VIEW_Thumbs) continue;
	//}
	if (menuactions.n) {
		ActionBox *b;
		for (int c=0; c<menuactions.n; c++) {
			b=menuactions.e[c];

			if (b==currentactionbox) foreground_color(coloravg(win_colors->fg,win_colors->bg,.6666));
			else foreground_color(coloravg(win_colors->fg,win_colors->bg,.9));

			fill_rectangle(this,b->minx,b->miny, b->maxx-b->minx,b->maxy-b->miny);
			foreground_color(win_colors->fg);
			textout(this, b->text.c_str(),-1, (b->maxx+b->minx)/2,(b->maxy+b->miny)/2, LAX_CENTER);
		}
	}
	if (currentactionbox) {
		ActionBox *b=currentactionbox;
		if (b->show_hover) {
			foreground_color(coloravg(win_colors->fg,win_colors->bg,.6666));
			fill_rectangle(this,b->minx,b->miny, b->maxx-b->minx,b->maxy-b->miny);
			//foreground_color(coloravg(win_colors->fg,win_colors->bg,.3));
			foreground_color(win_colors->fg);
			textout(this, b->text.c_str(),-1, (b->maxx+b->minx)/2,(b->maxy+b->miny)/2, LAX_CENTER);
		}
	}

	if (showmarkedpanel && selection.kids.n) {
		ShowMarkedPanel();
	}
}

/*! Draw adjacent thumbs to thumb.
 * Goes up one, does DrawThumbsRecurseDown() on all kids but thumb,
 */
void LivWindow::DrawThumbsRecurseUp(ImageThumb *thumb, double *m)
{
	if (!thumb->parent) return;

	ImageThumb *zone=thumb->parent;

	for (int c=0; c<zone->kids.n; c++) {
		if (zone->kids.e[c]==thumb) continue;

		if (*** zone->kids.e[c] out of bounds) continue;

		DrawThumbsRecurseDown(zone->kids.e[c], *** m);
	}
}

/*! Draw current thumb and any children.
 */
void LivWindow::DrawThumbsRecurseDown(ImageThumb *thumb, double *m)
{ ***
	*** if too small, return;

	for (int c=0; c<curzone->kids.n; c++) {
		*** if too small, return;

		if (viewmarked) {
			if ((curzone->kids.e[c]->mark&viewmarked)==0) continue;
		}
		n++;

		img=curzone->kids.e[c];
		e=1; //whether to draw image or an X
		x=imgt->tx;
		y=imgt->ty;
		w=imgt->tw-thumbgap;
		h=imgt->th-thumbgap; //pixel width of preview image

		if (h<=0 || w<=0) {
			iw=img->width;
			ih=img->height;
			if (iw<=0 || ih<=0) { iw=ih=100; }
			if (iw>ih) {
				w=previewsize;
				h=ih*previewsize/iw;
			} else {
				h=previewsize;
				w=iw*previewsize/ih;
			}
		}

		p[0]=transform_point(thumb_matrix,x,y); //upper left
		p[1]=transform_point(thumb_matrix,flatpoint(x,y)+flatpoint(w,0)); //upper right
		p[2]=transform_point(thumb_matrix,flatpoint(x,y)+flatpoint(w,h)); //lower right
		p[3]=transform_point(thumb_matrix,flatpoint(x,y)+flatpoint(0,h)); //lower left

		ww=norm(p[1]-p[0]);
		hh=norm(p[3]-p[0]);

		if (img->preview) {
			ii=imlib_load_image(img->preview);
			if (ii) {
				e=0;
				//DBG cerr <<"ONE  w,h:"<<w<<"x"<<h<<"  ww,hh:"<<ww<<'x'<<hh<<endl;

				imlib_context_set_image(ii);
				if (!(p[0].x+ww<0 || p[0].x>win_w || p[0].y+hh<0 || p[0].y>win_h))
					imlib_render_image_on_drawable_at_size(p[0].x,p[0].y, ww,hh);

				imlib_free_image();

				//DBG cerr<<"file "<<img->filename<<":  img->preview w,h:"<<w<<'x'<<h<<endl;
			}
		} 
		if (e) {
			 //draw box with x in it for no preview
			//DBG cerr <<"TWO  w,h:"<<w<<"x"<<h<<"  ww,hh:"<<ww<<'x'<<hh<<endl;

			draw_lines(this, p,4,1);
			draw_line(this, p[0].x,p[0].y, p[2].x,p[2].y);
			draw_line(this, p[1].x,p[1].y, p[3].x,p[3].y);
		}

		if (img->mark&1) {
			int maxh=app->defaultlaxfont->textheight()/2;
			hh/=6;
			if (hh>maxh) hh=maxh;

			draw_thing(this, p[2].x-hh,p[2].y-hh,hh,hh, THING_Diamond,rgbcolor(0,255,0),rgbcolor(0,255,0),1);
		}

	} //foreach image
}

/*! Screen refresh for VIEW_Normal mode.
 */
void LivWindow::RefreshNormal()
{
	if (!current) SelectImage(0);
	if (!current) {
		DBG cerr <<"no images to display, returning!"<<endl;
		pthread_mutex_unlock(&imlib_mutex);
		return;
	}


	if (overlay_color==NULL) {
		//laximlib_update_alpha(overlayalpha);

		overlay_color=imlib_create_image(40,40);
		//overlay_color=imlib_create_image_from_drawable(0, 0,0, 40,40, 1);
		//imlib_context_set_image(dynamic_cast<LaxImlibImage *>(current->image)->Image());
		//overlay_color=imlib_create_cropped_image(0,0,40,40);
		//overlay_color=imlib_clone_image();
		//overlay_color=imlib_load_image("testimage.png");
		imlib_context_set_image(overlay_color);
		imlib_image_set_has_alpha(1);
		imlib_context_set_color(0,0,255,255);
		imlib_image_fill_rectangle(0,0,40,40);

	}
	
	//static DoubleBBox box;
	if (current->image) {

		int w,h;
		w=current->image->w();
		h=current->image->h();
		
		//box.clear();
		flatpoint ul,ur,ll;
		ul=transform_point(current->matrix, 0,0);
		ur=transform_point(current->matrix, w,0);
		ll=transform_point(current->matrix, 0,h);

		if (screen_rotation!=0) {
			ul=transform_point(screen_matrix,ul);
			ur=transform_point(screen_matrix,ur);
			ll=transform_point(screen_matrix,ll);
		}

		ur=ur-ul;
		ll=ll-ul;
		//box.addtobounds(ul);
		//box.addtobounds(ur);
		//box.addtobounds(ll);
		//box.addtobounds(lr);
		//if (***any point is off screen) { ***draw only what is necessary
		//	box.clear();
		//	 //figure out how screen maps to image space
		//	transfrom_invert(mm,current->matrix);
		//	box.addtobounds(transform_point(mm,flatpoint(0,0)));
		//	box.addtobounds(transform_point(mm,flatpoint(0,win_h)));
		//	box.addtobounds(transform_point(mm,flatpoint(win_w,0)));
		//	box.addtobounds(transform_point(mm,flatpoint(win_w,win_h)));
		//
		//	 //clamp screen box to image dimensions
		//	if (box.minx<0) box.minx=0;
		//	if (box.miny<0) box.miny=0;
		//	if (box.maxx>current->width) box.maxx=current->width;
		//	if (box.maxy>current->height) box.maxy=current->height;
		// 
		//	 //remap relevant portion of image to screen
		//	ul=transform_point(current->matrix, box.minx,box.miny);
		//	ur=transform_point(current->matrix, box.maxx,box.miny)-ul;
		//	ll=transform_point(current->matrix, box.minx,box.maxy)-ul;
		//	imlib_render_image_on_drawable_skewed(box.minx,box.miny, 
		//					box.maxx-box.minx, box.maxy-box.miny,
		//					(int)ul.x,(int)ul.y,
		//					(int)ur.x,(int)ur.y,
		//					(int)ll.x,(int)ll.y);
		//}
		
//		imlib_context_set_drawable(drawable);
//		imlib_context_set_image(dynamic_cast<LaxImlibImage *>(current->image)->Image());
//		imlib_render_image_on_drawable_skewed(
//							0,0, 
//							w,h,
//							(int)ul.x,(int)ul.y,
//							(int)ur.x,(int)ur.y,
//							(int)ll.x,(int)ll.y);
		//image_out_skewed(current->image,this,
		image_out_skewed(current->image,this,
							//0,0, 
							//w,h,
							(int)ul.x,(int)ul.y,
							(int)ur.x,(int)ur.y,
							(int)ll.x,(int)ll.y);
		
		imlib_context_set_image(overlay_color);
		//imlib_render_image_on_drawable(win_w/4,win_h/4);
		//imlib_render_image_on_drawable_at_size(0,0, 200,200);
	}

	if (showoverlay) {
		imlib_context_set_image(overlay_color);
		laximlib_update_alpha(overlayalpha);
		laximlib_usealpha(1);
		//imlib_render_image_on_drawable(win_w/4,win_h/4);
		imlib_render_image_on_drawable_at_size(win_w/4,win_h/4, win_w/2,win_h/2);
		laximlib_usealpha(0);
	}

	int y=0;

	if (showbasics) {
		static char text[500];
		drawing_function(GXor);
		if (showbasics&SHOW_Filename) {
			sprintf(text,"%s",current->filename);
			if (curzone->kids.n>1) {
				sprintf(text+strlen(text),"  (%d/%d)",1+current_image_index,curzone->kids.n);
			}
			textout(this, text,-1,0,y,LAX_TOP|LAX_LEFT);
			y+=app->defaultlaxfont->textheight();
		}
		if (showbasics&SHOW_Index) {
			sprintf(text,"%d/%d",1+current_image_index,curzone->kids.n);
			textout(this,text,-1,0,y,LAX_TOP|LAX_LEFT);
			y+=app->defaultlaxfont->textheight();
		}
		if (showbasics&SHOW_Filesize) {
			double s=current->fileinfo.st_size;
			if (s<1024) {
				sprintf(text,"%d bytes",(int)current->fileinfo.st_size);
			} else if (s<1024*1024) {
				sprintf(text,"%ld kb",current->fileinfo.st_size/1024);
			} else {
				sprintf(text,"%.1f Mb",current->fileinfo.st_size/1024./1024);
			}

			textout(this,text,-1,0,y,LAX_TOP|LAX_LEFT);
			y+=app->defaultlaxfont->textheight();
		}
		if (showbasics&SHOW_Dims) {
			sprintf(text,"%d x %d",current->width,current->height);
			textout(this, text,-1,0,y,LAX_TOP|LAX_LEFT);
			y+=app->defaultlaxfont->textheight();
		}
		if (showbasics&SHOW_Tags) {
			for (int c=0; c<tagboxes.n; c++) {
				textout(this, tagboxes.e[c]->text.c_str(), -1, 
						(tagboxes.e[c]->minx+tagboxes.e[c]->maxx)/2,
						(tagboxes.e[c]->miny+tagboxes.e[c]->maxy)/2,
						LAX_CENTER);
			}

			 //draw new tag box
			foreground_color(coloravg(win_colors->fg,win_colors->bg));
			//DBG cerr <<"new tag text:"<<newtag->text.c_str()<<endl;
			textout(this, newtag->text.c_str(), -1, 
					(newtag->minx+newtag->maxx)/2,
					(newtag->miny+newtag->maxy)/2,
					LAX_CENTER);
			foreground_color(win_colors->fg);
		}
		drawing_function(GXcopy);
	}

	if (current->meta && showmeta) {
		textout_multiline(this, current->meta,-1, 0,y, LAX_LEFT|LAX_TOP);
	}

	if (verbose) {
		if (currentactionbox) {
			ActionBox *b=currentactionbox;
			if (!b->is_abs_dims) {
				double w=win_w; // *** must account for rotated screen!!
				double h=win_h; // *** must account for rotated screen!!
				textout(this, currentactionbox->text.c_str(),-1, (b->maxx+b->minx)/2*w,(b->maxy+b->miny)/2*h, LAX_CENTER);
				DBG cerr << "text "<<b->text<<" at "<<(b->maxx+b->minx)/2*w<<','<<(b->maxy+b->miny)/2*h<<endl;
			} else {
				DBG cerr << "text "<<b->text<<" at "<<(b->maxx+b->minx)/2<<','<<(b->maxy+b->miny)/2<<endl;
				textout(this, currentactionbox->text.c_str(),-1, (b->maxx+b->minx)/2,(b->maxy+b->miny)/2, LAX_CENTER);
			}
		}
	}

	if (showmarkedpanel && selection.kids.n) {
		ShowMarkedPanel();
	}

	 //when hovering over certain boxes, show highlighted
	if (currentactionbox) {
		ActionBox *b=currentactionbox;
		if (b->show_hover) {
			foreground_color(coloravg(win_colors->fg,win_colors->bg,.6666));
			fill_rectangle(this,b->minx,b->miny, b->maxx-b->minx,b->maxy-b->miny);
			foreground_color(coloravg(win_colors->fg,win_colors->bg,.3));
			textout(this, b->text.c_str(),-1, (b->maxx+b->minx)/2,(b->maxy+b->miny)/2, LAX_CENTER);
		}
	}
}

//! In lower right, show simple list of currently selected files.
/*! Called from Refresh().
 */
void LivWindow::ShowMarkedPanel()
{
	if (selection.kids.n) {
		ActionBox *b;
		ImageFile *img;
		Imlib_Image ii;
		for (int c=0; c<selboxes.n; c++) {
			b=selboxes.e[c];
			if (b->action==LIVA_Show_All_Selected) {
				 //draw gray background and text
				foreground_color(coloravg(win_colors->fg,win_colors->bg,.6666));
				fill_rectangle(this,b->minx,b->miny, b->maxx-b->minx,b->maxy-b->miny);
				foreground_color(win_colors->fg);
				textout(this, b->text.c_str(),-1, (b->maxx+b->minx)/2,(b->maxy+b->miny)/2, LAX_CENTER);

			} else if (b->action==LIVA_Show_Selected && b->index>=0 && b->index<selection.kids.n) {
				 //draw thumbnail
				img=selection.kids.e[b->index]->image;

				if (img==current && viewmode==VIEW_Normal) {
					int h=(b->maxy-b->miny)/2;
					draw_thing(this,win_w-h/2,win_h-2.5*h,h/2,h/2, THING_Diamond,rgbcolor(0,255,0),rgbcolor(0,255,0),1);
				}

				if (img->preview && img->width>0 && img->height>0) {
					ii=imlib_load_image(img->preview);
					if (ii) {
						imlib_context_set_image(ii);
						imlib_render_image_on_drawable_at_size(b->minx,b->miny, b->maxx-b->minx,b->maxy-b->miny);
						imlib_free_image();
					}
				}
			}
		}
	}
}

//! Save files, or a subset if viewmarked!=0.
/*! If viewmarked!=0, then do not clobber collectionfile. Only use file name here, but don't remember it.
 */
int LivWindow::SaveCollection(const char *file, ImageThumb *list)
{
	FILE *f=fopen(file,"w");
	if (!f) {
		DBG cerr <<"Could not open "<<file<<" for writing!!"<<endl;
		return 1;
	}

	if (!viewmarked) makestr(collectionfile,file);

	if (list==NULL) {
		if (viewmarked) list=&selection;
		else list=&collection;
	}

	for (int c=0; c<list->kids.n; c++) {
		dump_img(f,list->kids.e[c],0);
	}

	fclose(f);
	return 0;
}

/*! Write out the image info.
 */
void LivWindow::dump_img(FILE *f, ImageThumb *t, int indent)
{
	if (!t) return;
	ImageFile *i=t->i;
	if (!i) return;

	char spc[indent+1]; memset(spc,' ',indent); spc[indent]='\0';
	char *tags;

	if (i->type==ITEM_Is_File) {
		fprintf(f,"%sfile %s\n",spc,i->filename);

	} else if (i->type==ITEM_Is_Directory) {
		fprintf(f,"%sdirectory %s\n",spc,i->filename);

	} else if (i->type==ITEM_Is_Set) {
		fprintf(f,"%sset %s\n",spc,i->name?i->name:"\"Untitled\"");
	}

	 //tags
	if (i->NumberOfTags()) {
		fprintf(f,"%s  tags ",spc);
		tags=i->GetAllTags();
		fprintf(f,"%s\n",tags);
		delete[] tags;
	}
	if (i->title) {
		fprintf(f,"%s  title",spc);
		dump_out_value(f,indent+4,i->title);
	}
	if (i->description) {
		fprintf(f,"%s  description",spc);
		dump_out_value(f,indent+4,i->description);
	}

	if (i->meta) {
		fprintf(f,"%s  meta",spc);
		i->meta->dump_out(f,indent+4);
	}

	if (i->type==ITEM_Is_Set) {
		if (i->kids.n) {
			for (int c=0; c<i->kids.n; c++) {
				dump_img(f,i->kids.e[c],indent+2);
			}
		}
	}

}

int LivWindow::dump_in_img(ImageThumb *dest, LaxFiles::Attribute *att, const char *directory)
{
	*** //must check against files list, to not create unnecessary dups

	if (!dest || !att) return 1;

	char *name,*value;
	char *img;
	char *imgfile=NULL;
	char *iname;
	char *tags;
	char *title;
	char *desc;
	ImageFile *ii=NULL;
	Attribute *meta;
	int type;

	int numadded=0;

	for (int c=0; c<att->attributes.n; c++) {
		name= att->attributes.e[c]->name;
		value=att->attributes.e[c]->value;

		type=0;
		img=NULL;
		tags=NULL;
		title=NULL;
		desc=NULL;
		meta=NULL;
		iname=NULL;

		if (!strcmp(name,"file")) {
			type=ITEM_Is_File;
			img=value;
		} else if (!strcmp(name,"directory")) {
			type=ITEM_Is_Directory;
			img=value;
		} else if (!strcmp(name,"set")) {
			type=ITEM_Is_Set;
			iname=value;
		}

		for (int c2=0; c2<att->attributes.e[c]->attributes.n; c2++) {
			name= att->attributes.e[c]->attributes.e[c2]->name;
			value=att->attributes.e[c]->attributes.e[c2]->value;

			if (!strcmp(name,"tags")) {
				tags=value;

			} else if (!strcmp(name,"meta")) {
				if (meta) {
					DBG cerr <<"should append to att in ivWindow::dump_in_img()"<<endl;
					//delete ii->meta; // *** should append
				}
				meta=att->attributes.e[c]->attributes.e[c2];
				//ii->meta=att->attributes.e[c]->attributes.e[c2]->duplicate();

			} else if (!strcmp(name,"title")) {
				title=value;

			} else if (!strcmp(name,"description")) {
				desc=value;
			}
		}

		imgfile=full_path_for_file(img,directory);
		ii=new ImageFile(type,iname,imgfile,title,desc,meta);
		delete[] imgfile;
		dest->Add(ii);
		tt=dest->kids.e[dest->kids.n-1];
		numadded++;

		if (type==ITEM_Is_Set) {
			 // read in kids
			dump_in_img(tt, att->attributes.e[c], directory);
		}
	}

}

/*! Return 0 for success or nonzero for error.
 */
int LivWindow::LoadCollection(const char *file)
{
	DBG cerr <<"LoadCollection "<<file<<"..."<<endl;
	if (!file) return 1;

	char *coldir=lax_dirname(file,0);

	Attribute att;
	char *name,*value;
	char *img, *imgfile;
	char *tags;
	ImageFile *ii=NULL;
	if (att.dump_in(file,NULL)!=0) return 1;

	ImageThumb *dest=&collection;
	dump_in_img(dest,&att,coldir);

	if (!collectionfile) makestr(collectionfile,file);
	DBG cerr <<"LoadCollection done."<<endl;
	delete[] coldir;
	return 0;
}

int LivWindow::Event(const EventData *data,const char *mes)
{
	if (!strcmp(mes,"saveas")) {
		const StrEventData *s=dynamic_cast<const StrEventData*>(data);
		if (!s || isblank(s->str)) return 1;
		SaveCollection(s->str);
		return 0;

	} else if (!strcmp(mes,"newtag")) {
		if (!current) return 0;
		const StrEventData *s=dynamic_cast<const StrEventData*>(data);
		if (!s || isblank(s->str)) return 1;
		current->InsertTags(s->str,0);
		PositionTagBoxes();
		needtodraw=1;
		return 0;
	}

	return anXWindow::Event(data,mes);
}

void LivWindow::InitActions()
{
	imageactions.push(new ActionBox(_("(new tag)"),     LIVA_NewTag,0,       1,     0,0,  -1,-1,     1,VIEW_Normal), 1);
	newtag=imageactions.e[0];

	//imageactions.push(new ActionBox(_("Up"),            LIVA_Up,           0,1,  .5,.5,    0,0,      0,VIEW_Normal), 1);
	imageactions.push(new ActionBox(_("Previous"),      LIVA_Previous,0,     0,  0,1./3,   0,1.0,    0,VIEW_Normal), 1);
	imageactions.push(new ActionBox(_("Next"),          LIVA_Next,0,         0,  2./3,1.0, 0,1.0,    0,VIEW_Normal), 1);
	imageactions.push(new ActionBox(_("1:1"),           LIVA_Scale_1_To_1,0, 0,  0,1.0,    0,1./3,   0,VIEW_Normal), 1);
	imageactions.push(new ActionBox(_("Fit to screen"), LIVA_Fit_To_Screen,0,0,  0,1.0,    2./3,1.0, 0,VIEW_Normal), 1);

	int th=app->defaultlaxfont->textheight();
	imageactions.push(new ActionBox(_("Menu"),          LIVA_Menu,-1,         1,  win_w-2*th,0, win_w,2*th,  1,VIEW_Thumbs), 1);

	first_tag_action=imageactions.n;
}

/*! 0==normal, 1==rotate 90 degrees clockwise, 2==rotate 180, 3=rotate 90 degrees counterclockwise
 */
void LivWindow::RotateScreen(int howmuch)
{
	screen_rotation=howmuch;
	if (screen_rotation==0) {
		transform_set(screen_matrix,1,0,0,1,0,0);
	} else if (screen_rotation==1) { //90 clockwise
		transform_set(screen_matrix,0,1,-1,0,win_w,0);
	} else if (screen_rotation==2) { //180
		transform_set(screen_matrix,-1,0,0,-1, win_w,win_h);
	} else { //90 cc
		transform_set(screen_matrix,0,1,-1,0, 0,win_h);
	}
}

//! Return an id number for what action results from pressing a button down at this location in window.
/*! x,y are screen coordinates.
 *
 * boxindex gets assigned to the box->index field.
 */
ActionBox *LivWindow::GetAction(int x,int y,unsigned int state, int *boxindex)
{
	double w,h;
	 //rotation:
	 //  0 normal: --> x, | y
	 //                   v 
	 //               |
	 //  1   <--- y,  v x
	 //
	 //  2   ^ y,   <---x
	 //      |
	 //
	 //  3   ---> y, ^ x
	 //              | 
	
	if (screen_rotation==0) {
		w=win_w; h=win_h; 
	} else if (screen_rotation==1) { //90 clockwise
		h=win_w; w=win_h; 
	} else if (screen_rotation==2) { //180
		w=win_w; h=win_h; 
	} else { //90 cc
		h=win_w; w=win_h; 
	}
	flatpoint p=transform_point_inverse(screen_matrix,flatpoint(x,y));
	double xx,yy;  //(xx,yy) is a coordinate with values where (1,1)==(w,h)
	xx=p.x/w;
	yy=p.y/h;


	//DBG dumpctm(screen_matrix);
	DBG cerr <<"w="<<w<<" h="<<h<<"  ("<<x<<','<<y<<") -> ("<<xx<<','<<yy<<")"<<"==("<<p.x<<','<<p.y<<")"<<endl;

	// ****** checking for boxes should be refined a bit!!!

	if (viewmode==VIEW_Normal) {
		 //check tag boxes
		for (int c=0; c<tagboxes.n; c++) {
			if (x>=tagboxes.e[c]->minx && x<tagboxes.e[c]->maxx 
					&& y>=tagboxes.e[c]->miny && y<tagboxes.e[c]->maxy) {
				if (boxindex) *boxindex=tagboxes.e[c]->index;
				return tagboxes.e[c];
			}
		}

		 //check sel boxes
		for (int c=0; c<selboxes.n; c++) {
			if (x>=selboxes.e[c]->minx && x<selboxes.e[c]->maxx 
					&& y>=selboxes.e[c]->miny && y<selboxes.e[c]->maxy) {
				if (boxindex) *boxindex=selboxes.e[c]->index;
				return selboxes.e[c];
			}
		}
	}

	 //check for mouse over action boxes
	ActionBox *box=GetAction(actions, x,y,state,boxindex);
	if (box) return box;

	box=GetAction(&menuactions, x,y,state,boxindex);
	if (box) return box;

	return NULL;
}

/*! From position on screen, find which action area we are inside.
 */
ActionBox *LivWindow::GetAction(PtrStack<ActionBox> *alist, int x,int y,unsigned int state, int *boxindex)
{
	double w,h;
	if (screen_rotation==0) {
		w=win_w; h=win_h; 
	} else if (screen_rotation==1) { //90 clockwise
		h=win_w; w=win_h; 
	} else if (screen_rotation==2) { //180
		w=win_w; h=win_h; 
	} else { //90 cc
		h=win_w; w=win_h; 
	}
	flatpoint p=transform_point_inverse(screen_matrix,flatpoint(x,y));
	double xx,yy;  //(xx,yy) is a coordinate with values where (1,1)==(w,h)
	xx=p.x/w;
	yy=p.y/h;

	 //check for mouse over action boxes
	for (int c=0; c<alist->n; c++) {
//		if (c==alist->n-1) { //*******
//			DBG cerr <<"  box: x:"<<alist->e[c]->minx<<","<<alist->e[c]->maxx
//					 <<"  y:"<<alist->e[c]->miny<<","<<alist->e[c]->maxy<<endl;
//
//		}

		//DBG cerr <<"mode="<<viewmode<<"  action mode="<<alist->e[c]->mode<<endl;
		if (alist->e[c]->mode!=0 && alist->e[c]->mode!=viewmode) continue;
		//DBG cerr <<"passed mode check!"<<endl;

		if (alist->e[c]->is_abs_dims==2) { //real coords
			 //coordinate (x1,y2) is still 0..1, but (w,h)==(x2,y2) is absolute screen dimensions
			p.x=alist->e[c]->minx * w;
			p.y=alist->e[c]->minx * h;
			if (x>=p.x-alist->e[c]->maxx/2 && x<p.x+alist->e[c]->maxx/2 
				  && y>=p.y-alist->e[c]->maxy/2 && y<p.y+alist->e[c]->maxy/2) {
				if (boxindex) *boxindex=c;
				return alist->e[c];
			}

		} else if (alist->e[c]->is_abs_dims==1) { //screen coords
			if (x>=alist->e[c]->minx && x<alist->e[c]->maxx 
					&& y>=alist->e[c]->miny && y<alist->e[c]->maxy) {
				if (boxindex) *boxindex=c;
				return alist->e[c];
			}

		} else if (xx>=alist->e[c]->minx && xx<alist->e[c]->maxx  //coord space is 0..1,0..1
				&& yy>=alist->e[c]->miny && yy<alist->e[c]->maxy) {
			if (boxindex) *boxindex=c;
			return alist->e[c];
		}
	}

	return NULL;
}

//! Activate menu if not on, or turn off if on.
int LivWindow::ToggleMenu()
{
	if (menuactions.n) {
		 //turn off
		menuactions.flush();
		needtodraw=1;
		return 0;
	}

	 //turn on
	int th=app->defaultlaxfont->textheight();
	int w,x2=win_w-2*th;
	int y=0;
	w=getextent(_("Filename Caseless"),-1, NULL,NULL)+th;

	menuactions.push(new ActionBox(_("Sort"),              LIVA_None,-1,                 1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("Reverse"),           LIVA_Sort_Reverse,-1,         1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("Date"),              LIVA_Sort_Date,-1,            1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("File size"),         LIVA_Sort_Filesize,-1,        1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("Area"),              LIVA_Sort_Area,-1,            1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("Width"),             LIVA_Sort_Width,-1,           1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("Height"),            LIVA_Sort_Height,-1,          1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("Random"),            LIVA_Sort_Random,-1,          1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("Filename"),          LIVA_Sort_Filename,-1,        1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;
	menuactions.push(new ActionBox(_("Filename Caseless"), LIVA_Sort_FilenameCaseless,-1,1,  x2-w,x2, y,y+2*th, 1,VIEW_Thumbs), 1); y+=2*th;

	needtodraw=1;
	return 0;
}

int LivWindow::LBDown(int x,int y,unsigned int state,int count, const LaxMouse *d)
{
	DBG cerr<<"LivWindow::LBDown() id="<<d->id<<endl;

	buttondown.down(d->id,LEFTBUTTON, x,y);
	if (device1==0) device1=d->id;
	else if (device2==0) device2=d->id;
	lbdown=flatpoint(x,y);
	//lbdownaction=GetAction(x,y,state);

	hover_image=-1;
	return 0;
}

int LivWindow::LBUp(int x,int y,unsigned int state, const LaxMouse *d)
{ ***
	DBG cerr<<"LivWindow::LBUp() id="<<d->id<<endl;

	if (!buttondown.isdown(d->id,LEFTBUTTON)) return 0;
	mousemoved=buttondown.up(d->id,LEFTBUTTON);
	DBG cerr <<"***lbup mousemoved="<<mousemoved<<endl;
	if (mousemoved<7) mousemoved=0;

	if (d->id==device2) device2=0;
	else if (d->id==device1) { device1=device2; device2=0; }

	if (viewmode==VIEW_Thumbs) {
		if (mousemoved) return 0; //was just dragging

		int index=0;
		ActionBox *actionbox=GetAction(x,y,state,&index);
		int action=(actionbox?actionbox->action:LIVA_None);
		if (action!=LIVA_None) {
			if (action==LIVA_Menu) {
				if (index==-1) return 0;
				currentactionbox=NULL;
				ToggleMenu();
				return 0;

			}
			
			if (action==LIVA_Sort_Reverse) {
				ReverseOrder();
				currentactionbox=NULL;
				ToggleMenu();
				return 0;
			}

			const char *sort=NULL;
			if (action==LIVA_Sort_Date) sort="date";
			else if (action==LIVA_Sort_Filesize) sort="size";
			else if (action==LIVA_Sort_Area) sort="pixels";
			else if (action==LIVA_Sort_Width) sort="width";
			else if (action==LIVA_Sort_Height) sort="height";
			else if (action==LIVA_Sort_Filename) sort="name";
			else if (action==LIVA_Sort_FilenameCaseless) sort="casename";
			else if (action==LIVA_Sort_Random) sort="random";
			Sort(sort);
			currentactionbox=NULL;
			ToggleMenu();
			return 0;

		} else if (action==LIVA_None && menuactions.n) {
			currentactionbox=NULL;
			ToggleMenu();
			return 0;
		}

		 //view image under mouse
		int img=findImageAtCoord(x,y);
		if (img>=0) {
			SelectImage(img);
			lastviewjump=1;
			Mode(VIEW_Normal);
			needtodraw=1;
			return 0;
		}
		PerformAction(LIVA_Fit_To_Screen);
		needtodraw=1;
		return 0;
	}

	if (viewmode==VIEW_Help) {
		Mode(VIEW_Normal);
		needtodraw=1;
		return 0;
	}

	if (mousemoved) return 0;

	 //What follows are all basically button actions
	int index=0;
	ActionBox *actionbox=GetAction(x,y,state,&index);
	int action=(actionbox?actionbox->action:LIVA_None);

	PerformAction(action);


	return 0;
}

int LivWindow::MBDown(int x,int y,unsigned int state,int count, const LaxMouse *d)
{
	buttondown.down(d->id,MIDDLEBUTTON, x,y);

	mx=x; my=y;
	//if (current) mbdown=transform_point_inverse(current->matrix,flatpoint(x,y));
	if (current) mbdown=flatpoint(x,y);

	return 0;
}

int LivWindow::MBUp(int x,int y,unsigned int state, const LaxMouse *d)
{
	buttondown.up(d->id,MIDDLEBUTTON);
	return 0;
}

int LivWindow::RBDown(int x,int y,unsigned int state,int count, const LaxMouse *d)
{
	buttondown.down(d->id,RIGHTBUTTON, x,y);
	return 0;
}

int LivWindow::RBUp(int x,int y,unsigned int state, const LaxMouse *d)
{ ***
	buttondown.up(d->id,RIGHTBUTTON);

	if (!current) return 0;

	flatpoint pi=transform_point_inverse(current->matrix,flatpoint(x,y));
	PerformAction(LIVA_Scale_1_To_1);

	flatpoint o=flatpoint(x,y)-transform_point(current->matrix,pi);
	current->matrix[4]+=o.x;
	current->matrix[5]+=o.y;

	return 0;
}

//! Wheel up.
int LivWindow::WheelUp(int x,int y,unsigned int state, int count, const LaxMouse *d)
{ ***
	if (viewmode==VIEW_Thumbs) {
		 //zoom in

		 //if current mouse over image is more than 2/3 screen size, zoom to normal
		flatpoint p=transform_point_inverse(thumb_matrix, flatpoint(x,y));
		for (int c=0; c<curzone->kids.n; c++) {
			if (curzone->width<=0) continue;
			if (p.x>=curzone->kids.e[c]->x
					&& p.x<curzone->kids.e[c]->t+curzone->kids.e[c]->width
					&& p.y>=curzone->kids.e[c]->y
					&& p.y<curzone->kids.e[c]->y+curzone->kids.e[c]->height) {
				double tw=norm(transform_vector(thumb_matrix, flatpoint(curzone->kids.e[c]->width,0)));
				double th=norm(transform_vector(thumb_matrix, flatpoint(0,curzone->kids.e[c]->height)));
				DBG cerr <<"mouse in "<<c<<",  w,h:"<<tw<<','<<th<<endl;
				if (tw>win_w*2/3 || th>win_h*2/3) {
					SelectImage(c);
					lastviewjump=0;
					Mode(VIEW_Normal);
					needtodraw=1;
					return 0;
				}
				break;
			} 
		}

		 //we are not selecting an image, so still in thumb view
		flatpoint op=transform_point_inverse(thumb_matrix,flatpoint(x,y));
		for (int c=0; c<6; c++) thumb_matrix[c]/=.85;
		flatpoint np=transform_point(thumb_matrix,op);
		np=flatpoint(x,y)-np;
		thumb_matrix[4]+=np.x;
		thumb_matrix[5]+=np.y;
		needtodraw=1;
		return 0;
	}


	if ((state&LAX_STATE_MASK)==0) {
		ActionBox *box=GetAction(x,y,state);
		int action=box?box->action:LIVA_None;
		if (action==LIVA_Next_Image || action==LIVA_Prev_Image) {
			SelectImage((current_image_index+curzone->kids.n-1)%curzone->kids.n);
			needtodraw=1;
			return 0;
		}
	} else if ((state&LAX_STATE_MASK)==ControlMask) {
		Zoom(flatpoint(x,y),1/.85);
	}

	return 0;
}

//! Wheel down.
int LivWindow::WheelDown(int x,int y,unsigned int state, int count, const LaxMouse *d)
{ ***
	if ((state&LAX_STATE_MASK)==0) {
		ActionBox *box=GetAction(x,y,state);
		int action=box?box->action:LIVA_None;
		if (action==LIVA_Next_Image || action==LIVA_Prev_Image) {
			SelectImage((current_image_index+1)%curzone->kids.n);
			needtodraw=1;
			return 0;
		}
	} else if ((state&LAX_STATE_MASK)==ControlMask) {
		Zoom(flatpoint(x,y),.85);
	}

	if (viewmode==VIEW_Normal && (state&LAX_STATE_MASK)==0) {
		 //wheel out of VIEW_Normal into VIEW_Thumbs

		 //We wheel zoomed into this image, so wheel zoom out, centering on same spot in image
		if (lastviewjump==0) {
			//*** set zoom also?

			 //shift thumb view so same point in image is under mouse still
			flatpoint p=transform_point_inverse(current->matrix,flatpoint(x,y));
			double xx=p.x/current->width; //xx,yy 0..1 corresponds to image bounds
			double yy=p.y/current->height;

			xx*=curzone->kids.e[current_image_index]->width; //now scaled to thumb w,h
			yy*=curzone->kids.e[current_image_index]->height;

			xx+=curzone->kids.e[current_image_index]->x;
			yy+=curzone->kids.e[current_image_index]->y;
			
			flatpoint np=transform_point(thumb_matrix,flatpoint(xx,yy));
			np.x=x-np.x;
			np.y=y-np.y;

			thumb_matrix[4]+=np.x;
			thumb_matrix[5]+=np.y;

		} else {
			//maybe just shift screen 
		}
		Mode(VIEW_Thumbs);
		needtodraw=1;
		return 0;
	}

	if (viewmode==VIEW_Thumbs) {
		 //zoom out
		flatpoint op=transform_point_inverse(thumb_matrix,flatpoint(x,y));
		for (int c=0; c<6; c++) thumb_matrix[c]*=.85;
		flatpoint np=transform_point(thumb_matrix,op);
		np=flatpoint(x,y)-np;
		thumb_matrix[4]+=np.x;
		thumb_matrix[5]+=np.y;
		needtodraw=1;
		return 0;
	}

	return 0;
}

//! For the thumb view, return the index in files of the image at screen position x,y, or -1 if not over any image.
ImageThumb *LivWindow::findImageAtCoord(int x,int y, int *index_in_parent)
{ ***
	flatpoint p=transform_point_inverse(thumb_matrix, flatpoint(x,y));

	for (int c=0; c<curzone->kids.n; c++) {
		if (curzone->kids.e[c]->width<=0) continue;
		if (p.x>=curzone->kids.e[c]->x
				&& p.x<curzone->kids.e[c]->t+curzone->kids.e[c]->width
				&& p.y>=curzone->kids.e[c]->y
				&& p.y<curzone->kids.e[c]->y+curzone->kids.e[c]->height) {
			return curzone->kids.e[c];
		} 
	}
	*** if adjacent areas on screen, try those
	return -1;
}

int LivWindow::MouseMove(int x,int y,unsigned int state, const LaxMouse *d)
{ ***
	ActionBox *oldaction=currentactionbox;

	int aindex=0;
	currentactionbox=GetAction(x,y,state, &aindex);

	int oldx,oldy;
	buttondown.move(d->id, x,y, &oldx,&oldy);

	DBG cerr <<"old action: "<<(oldaction?oldaction->text.c_str():"no old action")<<"  aindex="<<aindex<<endl;
	DBG if (currentactionbox) cerr<<"currentaction:"<<currentactionbox->text.c_str()<<"  aindex="<<aindex<<endl; else cerr<<"no current action"<<endl;
	//DBG cerr <<"buttondown.any(0):"<<buttondown.any(0)<<endl;

	//DBG cerr <<"buttondown:"
	//DBG	<<buttondown.isdown(d->id,LEFTBUTTON)
	//DBG	<<buttondown.isdown(d->id,MIDDLEBUTTON)
	//DBG	<<buttondown.isdown(d->id,RIGHTBUTTON)
	//DBG <<"    d1,d2="<<device1<<','<<device2<<"  moved: "<<moved<<endl;

	if (viewmode==VIEW_Thumbs && !buttondown.any(0)) {
		 //hover the file name
		if (hover_image>=curzone->kids.n) hover_image=-2;
		int old=hover_image;
		hover_image=findImageAtCoord(x,y);
//		if (hover_image<0) hover_image=findImageAtCoord(x,y);
//		else if (x<curzone->e[hover_image]->tx
//				 || x>curzone->e[hover_image]->tx+curzone->e[hover_image]->tw
//				 || y<curzone->e[hover_image]->ty
//				 || y>curzone->e[hover_image]->ty+curzone->e[hover_image]->th
//				)
//			hover_image=findImageAtCoord(x,y);

		if (old!=hover_image && hover_image>=0) {
			makestr(hover_text,curzone->kids.e[hover_image]->filename);
			int w,h, xo=0,yo=0;
			flatpoint p=transform_point(thumb_matrix,
										flatpoint(curzone->kids.e[hover_image]->x+curzone->kids.e[hover_image]->width/2,
										curzone->kids.e[hover_image]->y+curzone->kids.e[hover_image]->height));

			DBG double tw=norm(transform_vector(thumb_matrix, flatpoint(curzone->kids.e[hover_image]->width,0)));
			DBG double th=norm(transform_vector(thumb_matrix, flatpoint(0,curzone->kids.e[hover_image]->height)));
			DBG cerr <<"------- mouse in "<<hover_image<<",  w,h:"<<tw<<','<<th<<endl;

			getextent(hover_text,-1,&w,&h);
			w+=h/2;
			h*=1.5;

			xo=p.x-w/2;
			yo=p.y;

			if (yo+h>win_h) yo=win_h-h;
			else if (yo<0) yo=0;
			if (xo+w>win_w) xo=win_w-w;
			else if (xo<0) xo=0;

			hover_area.minx=xo;
			hover_area.miny=yo;
			hover_area.maxx=xo+w;
			hover_area.maxy=yo+h;

			needtodraw=1;
		}
	}

	if (!buttondown.any(0) || !current) {
		if (oldaction!=currentactionbox) needtodraw=1;
		return 0;
	}

	flatpoint moveto(x,y);
	 //must check for this!! touch screen seems to send a move in between down and up events
	if (moveto.x!=lbdown.x || moveto.y!=lbdown.y) mousemoved=1;

//	if (viewmode==VIEW_Thumbs) {
//		if (!buttondown.isdown(d->id,LEFTBUTTON)) return 0;
//		//int oldx,oldy;
//		//buttondown.getlast(d->id,LEFTBUTTON, &oldx,&oldy);
//		thumb_matrix[4]+=x-oldx;
//		thumb_matrix[5]+=y-oldy;
//		needtodraw=1;
//		return 0;
//	}

	 //2 mouse movements
	double *m;
	if (viewmode==VIEW_Normal) m=current->matrix;
	else if (viewmode==VIEW_Thumbs) m=thumb_matrix;

	if (buttondown.isdown(0,LEFTBUTTON)>1 && device1>0 && device2>0 && (d->id==device1 || d->id==device2)) {
		 //double move
		int xp1,yp1, xp2,yp2;
		int xc1,yc1, xc2,yc2;
		buttondown.getinfo(device1,LEFTBUTTON, NULL,NULL, &xp1,&yp1, &xc1,&yc1);
		buttondown.getinfo(device2,LEFTBUTTON, NULL,NULL, &xp2,&yp2, &xc2,&yc2);

		double oldd=norm(flatpoint(xp1,yp1)-flatpoint(xp2,yp2));
		double newd=norm(flatpoint(xc1,yc1)-flatpoint(xc2,yc2));
		double zoom=newd/oldd;

		flatpoint oldp, newp, tp;
		if (d->id==device2) oldp=flatpoint(xc1,yc1); else oldp=flatpoint(xc2,yc2);
		tp=transform_point_inverse(m,oldp);

		for (int c=0; c<6; c++) m[c]*=zoom;

		newp=transform_point(m,tp);
		tp=newp-oldp;

		m[4]-=tp.x;
		m[5]-=tp.y;
		
		needtodraw=1;
		return 0;
	}

	 //single left
	if (buttondown.isdown(d->id,LEFTBUTTON)) {
		DBG cerr <<"----move LEFTBUTTON for "<<d->id<<endl;
		int mx,my;
		buttondown.getlast(d->id,LEFTBUTTON, &mx,&my);
		m[4]+=x-mx;
		m[5]+=y-my;

		needtodraw=1;
		return 0;

	 //single middle
	} else if (buttondown.isdown(d->id,MIDDLEBUTTON)) {
		DBG cerr <<"----move MIDDLEBUTTON for "<<d->id<<endl;

		if (!current) return 0;

		if (x-mx) {
			double s=x-mx;
			s=pow(1.01,s);

			Zoom(mbdown,s);
		}
		mx=x; my=y;
		return 0;
	}
	return 0;
}

//! Return whether a scaled image is approximately 3 times the screen size
int LivWindow::toobig()
{
	flatpoint p1,p2;
	p1=transform_point(current->matrix,current->width,current->height);
	p2=transform_point(current->matrix,0,0);
	return (p1-p2)*(p1-p2)>9*(win_h*win_h+win_w*win_w);
}

//! Zoom current image with screen point center staying at the same place.
void LivWindow::Zoom(flatpoint center,double s)
{
	if (!current || !(current->state&FILE_Has_image)) return;
	flatpoint p=transform_point_inverse(current->matrix,center);

	flatpoint x(current->matrix[0],current->matrix[1]),
			  y(current->matrix[2],current->matrix[3]);

	 //**** screwy zoom bounds checking
	if (s>1 && norm(x)>4 && toobig()) return;//***need limits for image scale size, rather than mag!!!
	else if (s<1 && norm(x)<.01) return;//***need limits for image scale size, rather than mag!!!

	x*=s;
	y*=s;
	current->matrix[0]=x.x;
	current->matrix[1]=x.y;
	current->matrix[2]=y.x;
	current->matrix[3]=y.y;

	flatpoint o=center-transform_point(current->matrix,p);
	current->matrix[4]+=o.x;
	current->matrix[5]+=o.y;

	needtodraw=1;
}

Laxkit::ShortcutHandler *LivWindow::GetShortcuts()
{
	if (sc) return sc;
	ShortcutManager *manager=GetDefaultShortcutManager();
	sc=manager->NewHandler(whattype());
	if (sc) return sc;

	//virtual int Add(int nid, const char *nname, const char *desc, const char *icon, int nmode, int assign);

	sc=new ShortcutHandler(whattype());

	sc->AddMode(VIEW_Slideshow, "slideshow", _("Slideshow"), _("Playing a slideshow"));
	sc->AddMode(VIEW_Normal,    "normal",    _("Normal"),    _("View single images at a time"));
	sc->AddMode(VIEW_Thumbs,    "browse",    _("Thumbs"),    _("View and navigate thumbnails"));

	 //slideshow mode:
	sc->Add(LIVA_Pause,              ' ',0,VIEW_Slideshow,               "Pause",    _("Pause"),NULL,0);
	sc->Add(LIVA_Beginning,          LAX_Left,ShiftMask,VIEW_Slideshow,  "Beginning",_("Go to beginning"),NULL,0);
	sc->Add(LIVA_End,                LAX_Right,ShiftMask,VIEW_Slideshow, "End",      _("Go to end"),NULL,0);

	sc->Add(LIVA_Previous,           LAX_Left,0,0,    "Previous",       _("Previous"),NULL,0);
	sc->Add(LIVA_Next,               LAX_Right,0,0,   "Next",           _("Next"),NULL,0);
	sc->Add(LIVA_Up,                 LAX_Up,0,0,      "Up",             _("Up"),NULL,0);

	sc->Add(LIVA_Play,               'p',0,0,                            "Play",     _("Play"),NULL,0);

	sc->Add(LIVA_Scale_1_To_1        '1',0,0,         "OneToOne",       _("Set scale 1:1"),NULL,0);
	sc->Add(LIVA_Fit_To_Screen,      ' ',0,0,         "CenterAndScale", _("Center image and scale to screen"),NULL,0);
	sc->Add(LIVA_Center,             ' ',ShiftMask,0, "Center",         _("Center image"),NULL,0);
	sc->Add(LIVA_NewTag              't',0,0,         "NewTag",         _("NewTag"),NULL,0);
	sc->Add(LIVA_EditTag,            'T',ShiftMask,0, "EditTag",        _("EditTag"),NULL,0);
	sc->Add(LIVA_Select,             'm',0,0,         "SelectCurrent",  _("Select current image"),NULL,0);
	sc->Add(LIVA_Remove,             LAX_Bksp,0,0,    "RemoveImage",    _("Remove current image"),NULL,0);
	sc->AddShortcut(LAX_Del,0,0,LIVA_Remove);

	sc->Add(LIVA_RotateScreen,       'r',ControlMask,0,          "RotateScreen", _("Rotate screen"),NULL,0);
	sc->Add(LIVA_RotateScreenR,      'R',ControlMask|ShiftMask,0,"RotateScreenR",_("Rotate screen"),NULL,0);
	sc->Add(LIVA_RotateImage,        'r',0,0,         "RotateImage",    _("Rotate screen"),NULL,0);
	sc->Add(LIVA_RotateImageR,       'R',ShiftMask,0, "RotateImageR",    _("Rotate screen"),NULL,0);

	sc->Add(LIVA_ToggleMeta,         'e',0,0,         "ToggleMeta",     _("Toggle showing meta info"),NULL,0);
	sc->Add(LIVA_ToggleInfo,         'f',0,0,         "ToggleInfo",     _("Toggle showing file info"),NULL,0);
	sc->Add(LIVA_ZoomIn,             '=',0,0,         "ZoomIn",         _("Zoom in"),NULL,0);
	sc->Add(LIVA_ZoomOut,            '-',0,0,         "ZoomOut",        _("Zoom out"),NULL,0);
	sc->Add(LIVA_ToggleFullscreen,   LAX_F11,0,0,     "ToggleFullscreen",_("Toggle fullscreen"),NULL,0);
	sc->Add(LIVA_ReplaceWithSelected,'B',ShiftMask,0, "ReplaceWithSelected",_("Replace collection with currently selected"),NULL,0);
	sc->Add(LIVA_Show_All_Selected,  LAX_F2,0,0,      "Selected",        _("Show all selected"),NULL,0);
	sc->Add(LIVA_ToggleBrowse,       't',ControlMask,0,"ToggleBrowse",   _("Toggle thumbnail browsing"),NULL,0);
	sc->Add(LIVA_RemapThumbs,        ' ',ControlMask,0,"RemapThumbs",    _("Map thumbs to screen with current scaling"),NULL,0);

	
	sc->Add(LIVA_SaveCollection,     's',ControlMask,0,"SaveCollection",_("Save collection"),NULL,0);

	sc->Add(LIVA_Menu,                 LAX_Menu,0,0,  "Menu",                   _("Toggle Menu"),NULL,0);
	sc->Add(LIVA_Sort_Date,            '2',0,0,        "Sort_Date",              _("Sort by date"),NULL,0);
	sc->Add(LIVA_Sort_Filesize,        '3',0,0,        "Sort_Filesize",          _("Sort by file size"),NULL,0);
	sc->Add(LIVA_Sort_Area,            '4',0,0,        "Sort_Area",              _("Sort by pixel count"),NULL,0);
	sc->Add(LIVA_Sort_Width,           '5',0,0,        "Sort_Width",             _("Sort by width"),NULL,0);
	sc->Add(LIVA_Sort_Height,          '6',0,0,        "Sort_Height",            _("Sort by height"),NULL,0);
	sc->Add(LIVA_Sort_Filename,        '7',0,0,        "Sort_Filename",          _("Sort by filename"),NULL,0);
	sc->Add(LIVA_Sort_FilenameCaseless,'8',0,0,        "Sort_FilenameCaseless",  _("Sort by filename, ignoring case"),NULL,0);
	sc->Add(LIVA_Sort_Reverse,         '9',0,0,        "Sort_Reverse",           _("Reverse order"),NULL,0);
	sc->Add(LIVA_Sort_Random,          '0',0,0,        "Sort_Random",            _("Randomize"),NULL,0);

	sc->Add(LIVA_Verbose,              'v',0,0,       "Verbose",                _("Toggle verbosity"),NULL,0);
	sc->Add(LIVA_Help,                 LAX_F1,0,0,    "Help",                   _("Show help"),NULL,0);
	sc->Add(LIVA_Quit,                 'q',0,0,       "Quit",                   _("Quit"),NULL,0);
	sc->AddShortcut(LAX_Esc,0,0,LIVA_Quit);

	manager->AddArea(whattype(),sc);
	return sc;
}



int LivWindow::PerformAction(int action)
{
	if (action==LIVA_None) return 0;

	if (action==LIVA_Next) {
		SelectImage((current_image_index+1)%curzone->kids.n);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_Previous) {
		SelectImage((current_image_index+curzone->kids.n-1)%curzone->kids.n);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_Play) {
	    if (viewmode==VIEW_Slideshow) {
			 //turn off slideshow
			Mode(VIEW_Normal);
			if (timerid) app->removetimer(this,timerid);
			timerid=0;
			return 0;

		} else {
			StartSlideshow();
			return 0;
		}

		return 0;

	} else if (action==LIVA_Pause) {
		cerr <<" *** must implement LiwWindow::LIVA_Pause!"<<endl;
		return 0;

	} else if (action==LIVA_Beginning) {
		cerr <<" *** must implement LiwWindow::LIVA_Beginning!"<<endl;
		return 0;

	} else if (action==LIVA_End) {
		cerr <<" *** must implement LiwWindow::LIVA_End!"<<endl;
		return 0;

	} else if (action==LIVA_Up) {
		cerr <<" *** must implement LiwWindow::LIVA_Up!"<<endl;
		return 0;


	} else if (action==LIVA_Scale_1_To_1) {
		 //scale 1:1 around center of screen
		if (!current) return 0;

		flatpoint p=transform_point_inverse(current->matrix,flatpoint(win_w/2,win_h/2));

		flatpoint x(current->matrix[0],current->matrix[1]),
				  y(current->matrix[2],current->matrix[3]);
		x/=norm(x);
		y/=norm(y);
		current->matrix[0]=x.x;
		current->matrix[1]=x.y;
		current->matrix[2]=y.x;
		current->matrix[3]=y.y;

		flatpoint o=flatpoint(win_w/2,win_h/2)-transform_point(current->matrix,p);
		current->matrix[4]+=o.x;
		current->matrix[5]+=o.y;

		needtodraw=1;
		return 0;

	} else if (action==LIVA_Fit_To_Screen || action==LIVA_Center) {
		if (!current || !current->width) return 0;

		if (action==LIVA_Fit_To_Screen) ScaleToFit(current);

		double W,H;
		if (screen_rotation==0 || screen_rotation==2) {
			W=win_w;
			H=win_h;
		} else {
			W=win_h;
			H=win_w;
		}
		flatpoint o=flatpoint(W/2,H/2)-transform_point(current->matrix,current->width/2,current->height/2);
		current->matrix[4]+=o.x;
		current->matrix[5]+=o.y;

		needtodraw=1;
		return 0;

	} else if (action==LIVA_NewTag) {
		app->addwindow(new LineEdit(this,_("New tag"),_("New tag"),
									   ANXWIN_ESCAPABLE|LINEEDIT_DESTROY_ON_ENTER|LINEEDIT_GRAB_ON_MAP,
									   x+15,y, 20*app->defaultlaxfont->textheight(),1.5*app->defaultlaxfont->textheight(), 5,
									   NULL,object_id,"newtag"));
//		app->rundialog(new InputDialog(NULL,_("New tag"),_("New tag"),ANXWIN_CENTER|ANXWIN_ESCAPABLE,
//									   0,0,30*app->defaultlaxfont->textheight(),0,0,
//									   NULL,object_id,"newtag",
//									   NULL, //start text
//									   NULL, //label
//									   _("OK"),BUTTON_OK,
//									   _("Cancel"),BUTTON_CANCEL));

	} else if (action==LIVA_EditTag) { //edit this tag
		app->addwindow(new LineEdit(this,_("Retag"),_("Retag"),
									   ANXWIN_ESCAPABLE|LINEEDIT_DESTROY_ON_ENTER|LINEEDIT_GRAB_ON_MAP,
									   x+15,y, 20*app->defaultlaxfont->textheight(),1.5*app->defaultlaxfont->textheight(), 5,
									   NULL,object_id,"retag",
									   current->GetTag(actionbox->index)));
		return 0;

	} else if (action==LIVA_Select) { //add the current image to marked images
		int i=-1;
		if (viewmode==VIEW_Normal) {
			if (!current) return 0;
			i=selection.kids.pushnodup(current);
		} else if (viewmode==VIEW_Thumbs) {
			if (hover_image<0 || hover_image>=curzone->kids.n) return 0;
			ImageFile *img=curzone->kids.e[hover_image];
			i=selection.kids.pushnodup(img);
		} else return 0;

		if (i>=0) {
			selection.kids.remove(i);
			current->mark&=~currentmark;
		} else current->mark|=currentmark;
		PositionSelectionBoxes();
		needtomap=2;
		needtodraw=1;
		return 0;

	} else if (action==LIVA_Remove) {
		 //remove current file from list
		files.remove(current_image_index);
		current=NULL;
		SelectImage(current_image_index);
		needtodraw=1;
		return 0;


	} else if (action==LIVA_RotateScreen || action==LIVA_RotateScreenR) {
		if (action==LIVA_RotateScreen) screen_rotation++; else screen_rotation--;
		if (screen_rotation>3) screen_rotation=0;
		else if (screen_rotation<0) screen_rotation=3;
		RotateScreen(screen_rotation);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_RotateImage || action==LIVA_RotateImageR) {
		if (viewmode==VIEW_Normal) {
			flatpoint o=transform_point(current->matrix,current->width/2,current->height/2);
			flatpoint x(current->matrix[0],current->matrix[1]);
			flatpoint y(current->matrix[2],current->matrix[3]);
			x=rotate(x,action==LIVA_RotateImage?90:-90,1);
			y=rotate(y,action==LIVA_RotateImage?90:-90,1);
			current->matrix[0]=x.x;
			current->matrix[1]=x.y;
			current->matrix[2]=y.x;
			current->matrix[3]=y.y;

			flatpoint o2=transform_point(current->matrix,current->width/2,current->height/2)-o;
			current->matrix[4]-=o2.x;
			current->matrix[5]-=o2.y;
			needtodraw=1;
		}
		
		return 0;

	} else if (action==LIVA_ToggleMeta) {
		if (!current) return 0;
		if (!current->meta) showmeta=1; else showmeta=!showmeta;

		if (!current->meta) current->fillinfo(FILE_Has_exif);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_ToggleInfo) {
		showbasics<<=1;
		showbasics|=1;
		if (showbasics>=SHOW_MAX) showbasics=0;
		needtodraw=1;
		return 0;

	} else if (action==LIVA_ZoomIn) {
		if (!current) return 0;

		//*** zoom in around mouse if in window
		//int mx,my;
		//mouseposition(this,&mx,&my,NULL);
		Zoom(flatpoint(win_w/2,win_h/2),1.2);
		return 0;

	} else if (action==LIVA_ZoomOut) {
		//*** zoom out around mouse if in window
		if (!current) return 0;
		Zoom(flatpoint(win_w/2,win_h/2),1./1.2);

		return 0;

	} else if (action==LIVA_ToggleFullscreen) {
		 //toggle full screen
		fullscreen=!fullscreen;
		DBG cerr <<"Toggle fullscreen: "<<fullscreen<<endl;

		//------------------------this should work?!? but doesn't:
		Atom _net_wm_state = XInternAtom(app->dpy, "_NET_WM_STATE", False);

		DBG cerr <<"***not sure how to toggle fullscreen after window is mapped!! unmap/map sort of works, but this doesn't!!"<<endl;
		XEvent ev;
		memset(&ev, 0, sizeof(ev));
		ev.xclient.type = ClientMessage;
		ev.xclient.message_type = _net_wm_state;
		ev.xclient.display = app->dpy;
		ev.xclient.window = xlib_window;
		ev.xclient.format = 32;
		//ev.xclient.data.l[0] = XInternAtom(app->dpy, "_NET_WM_STATE_TOGGLE",False);
		//Atom action=XInternAtom(app->dpy, (fullscreen?"_NET_WM_STATE_ADD":"_NET_WM_STATE_REMOVE"),False);
		//ev.xclient.data.l[0] = action;
		ev.xclient.data.l[0] = (fullscreen?1:0); //internet says: 0=no, 1=yes, 2 is toggle
		ev.xclient.data.l[1] = XInternAtom(app->dpy, "_NET_WM_STATE_FULLSCREEN", False);
		ev.xclient.data.l[2] = 0;
		ev.xclient.data.l[3] = 1;
		ev.xclient.data.l[4] = 0;
		if (!XSendEvent(app->dpy, DefaultRootWindow(app->dpy), False, SubstructureRedirectMask, &ev))
		//if (!XSendEvent(app->dpy, DefaultRootWindow(app->dpy), False, 0, &ev))
			cerr <<"could not set full screen"<<endl;

		//------------------------
		 //this doesn't work either:
		//Atom prop_fs = XInternAtom(app->dpy, "_NET_WM_STATE_FULLSCREEN", False);
		//Atom _net_wm_state = XInternAtom(app->dpy, "_NET_WM_STATE", False);
		//app->unmapwindow(this);
		//XEvent ev;
		//memset(&ev, 0, sizeof(ev));
		//ev.xclient.type = ClientMessage;
		//ev.xclient.message_type = _net_wm_state;
		//ev.xclient.display = app->dpy;
		//ev.xclient.window = xlib_window;
		//ev.xclient.format = 32;
		//ev.xclient.data.l[0] = (fullscreen ? 1 : 0);
		//ev.xclient.data.l[1] = prop_fs;
		//XChangeProperty(app->dpy, xlib_window, _net_wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&prop_fs, 1);
		//app->mapwindow(this);
		//------------------------
		return 0;


	} else if (action==LIVA_ReplaceWithSelected) {
		 //flush normal list, replace with marked list if any
		if (selection.kids.n==0) return 0;
		files.flush();
		collection.kids.flush();
		for (int c=0; c<selection.kids.n; c++) {
			files.push(selection.kids.e[c]);
			collection.kids.push(selection.kids.e[c]);
		}
		selection.kids.flush();
		SelectImage(0);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_Show_Selected_Image) {
		SelectImage(index);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_Show_All_Selected) { //limit view to marked images and switch to thumb view
		 // show the selected zone
		viewmarked=currentmark;
		Mode(VIEW_Thumbs);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_Show_Selected) {
		 //show selected image
		SelectImage(index);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_ToggleBrowse) {
		 //toggle normal/thumbnail view mode
		if (viewmode==VIEW_Normal) Mode(VIEW_Thumbs);
		else if (viewmode==VIEW_Thumbs) Mode(VIEW_Normal);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_RemapThumbs) {
		if (viewmode==VIEW_Thumbs) {
			thumbdisplaywidth=win_w/thumb_matrix[0];
			thumb_matrix[4]=thumb_matrix[5]=0;
			needtomap=2;
			needtodraw=1;
		}
		return 0;


	} else if (action==LIVA_SaveCollection) {
		app->rundialog(new FileDialog(NULL,_("Save collection as..."),_("Save collection as..."),
						ANXWIN_REMEMBER,
						0,0,0,0,0, object_id, "saveas",
						FILES_SAVE_AS|FILES_ASK_TO_OVERWRITE,
						viewmarked?"markedcollection.liv":
							collectionfile?collectionfile:"newcollection.liv"));
		return 0;

	} else if (action==LIVA_Verbose) {
		 //toggle verbosity
		verbose=!verbose;
		DBG cerr <<"verbose: "<<verbose<<endl;
		needtodraw=1;
		return 0;

	} else if (action==LIVA_Help) {
		 //go into help mode...
		lastmode=viewmode;
		Mode(VIEW_Help);
		needtodraw=1;
		return 0;

	} else if (action==LIVA_Quit) {
		app->quit();
		return 0;
	}


	 //menu and sorting actions
	if (action==LIVA_Menu) {
		currentactionbox=NULL;
		ToggleMenu();
		return 0;

	} else if (action==LIVA_Sort_Reverse) {
		ReverseOrder();
		currentactionbox=NULL;
		if (menuactions.n) ToggleMenu(); //turn off menu
		return 0;
	}

	const char *sort=NULL;
	if (action==LIVA_Sort_Date) sort="date";
	else if (action==LIVA_Sort_Filesize) sort="size";
	else if (action==LIVA_Sort_Area) sort="pixels";
	else if (action==LIVA_Sort_Width) sort="width";
	else if (action==LIVA_Sort_Height) sort="height";
	else if (action==LIVA_Sort_Filename) sort="name";
	else if (action==LIVA_Sort_FilenameCaseless) sort="casename";
	else if (action==LIVA_Sort_Random) sort="random";
	if (sort) {
		Sort(sort);
		currentactionbox=NULL;
		if (menuactions.n) ToggleMenu(); //turn off menu
		return 0;
	}

	return 1;
}

int LivWindow::CharInput(unsigned int ch, const char *buffer,int len,unsigned int state, const LaxKeyboard *d)
{
	//DBG cerr <<"char input"<<endl;


	if (viewmode==VIEW_Help) {
		 //any key gets out of help
		Mode(lastmode);
		DBG cerr <<"getting out of view help.."<<viewmode<<endl;
		needtodraw=1;
		return 0;
	}


	 //-------------any mode keys------------------
	if (ch==LAX_Esc) {
		if (menuactions.n) ToggleMenu();
		currentactionbox=NULL;
		needtodraw=1;
		return 0;

	} //end any mode keys


	if (viewmode==VIEW_Normal) {
		 //DBG *** -------testing overlays:		
		if (ch=='o') {
			//********temp overlay
			showoverlay=!showoverlay;
			needtodraw=1;
			return 0;

		} else if (ch=='0') {
			overlayalpha-=10;
			if (overlayalpha<0) overlayalpha=255;
			needtodraw=1;
			return 0;

		} else if (ch=='9') {
			overlayalpha+=10;
			if (overlayalpha>255) overlayalpha=0;
			needtodraw=1;
			return 0;

		}
	} //end VIEW_Normal keys


	if (!sc) GetShortcuts();
	int action=sc->FindActionNumber(ch,state&LAX_STATE_MASK,viewmode);
	if (action>=0) {
		return PerformAction(action);
	}


	return 1;
}

//! Set scaling of an image to fit the screen, but not the position.
void LivWindow::ScaleToFit(ImageFile *img)
{
	DoubleBBox box;
	double v,h, //vertical and horizontal height of image
		   s,   //scaling to apply to image
		   W,H; //rotated width and height of screen


	box.addtobounds(transform_point(img->matrix,0,0));
	box.addtobounds(transform_point(img->matrix,0,img->height));
	box.addtobounds(transform_point(img->matrix,img->width,0));
	box.addtobounds(transform_point(img->matrix,img->width,img->height));

	if (screen_rotation==0 || screen_rotation==2) {
		W=win_w;
		H=win_h;
	} else {
		W=win_h;
		H=win_w;
	}
	v=(box.maxy-box.miny)/H;
	h=(box.maxx-box.minx)/W;

	if (v>1 && h>1) if (v>h) s=v; else s=h;
	else if (v<1 && h<1) if (v>h) s=v; else s=h;
	else if (v>1) s=v;
	else if (h>1) s=h;
	else s=1;

	img->matrix[0]/=s;
	img->matrix[1]/=s;
	img->matrix[2]/=s;
	img->matrix[3]/=s;
}

//! Set the zoom on images if necessary.
void LivWindow::setzoom(ImageThumb *which)
{
	if (zoommode==LIVZOOM_Scale_To_Screen || zoommode==LIVZOOM_Shrink_To_Screen) {
		if (!which) which=curzone;

		 //zoom to fit in window always and center
		double W,H;
		if (screen_rotation==0 || screen_rotation==2) {
			W=win_w;
			H=win_h;
		} else {
			W=win_h;
			H=win_w;
		}
		flatpoint o;
		for (int c=0; c<which->kids.n; c++) {
			if (which->kids.e[c]->width<=0) continue;
			 //zoommode shrink to screen: fit in window if bigger, and center
			if (!(zoommode==LIVZOOM_Shrink_To_Screen && which->kids.e[c]->width<W && which->kids.e[c]->height<H)) 
				ScaleToFit(which->kids.e[c]);

			o=flatpoint(W/2,H/2)-transform_point(which->kids.e[c]->matrix,which->kids.e[c]->width/2,which->kids.e[c]->height/2);
			which->kids.e[c]->matrix[4]+=o.x;
			which->kids.e[c]->matrix[5]+=o.y;

			if (which->kids.e[c]->kids.n) setzoom(which->kids.e[c]);
		}
	}
}

int LivWindow::MoveResize(int nx,int ny,int nw,int nh)
{
	anXWindow::MoveResize(nx,ny,nw,nh);
	setzoom();
	RotateScreen(screen_rotation);
	PositionMiscBoxes();
	PositionTagBoxes();
	PositionSelectionBoxes();
	return 0;
}

int LivWindow::Resize(int nw,int nh)
{
	anXWindow::Resize(nw,nh);
	setzoom();
	RotateScreen(screen_rotation);
	PositionMiscBoxes();
	PositionTagBoxes();
	PositionSelectionBoxes();
	return 0;
}


//! Return 0 for selected, nonzero for error and file removed from list, maybe no more images...
/*! Note this means to make i the current image, NOT to add to selected images.
 */
int LivWindow::SelectImage(int i)
{
	int direction=1;
	if (current_image_index>=0) direction=i-current_image_index;

	if (curzone->kids.n==0) return 1;
	if (i>=curzone->kids.n) i=0;
	if (i<0) i=curzone->kids.n-1;
	if (i<0) {
		cerr <<"No more images!"<<endl;
		exit(0); //no more images!
	}
//	if (current && current->image) {
//		imlib_free_image(current->image);
//	}
	while (1) {
		current=curzone->kids.e[i];
		if (!current->image) current->fillinfo(FILE_Has_image);
		if (current->image || !(livflags&LIV_Autoremove)) break;

		 //Automatically remove any files that are not readable images
		DBG cerr <<"removing "<<current->filename<<" from list"<<endl;
		RemoveFile(i);

		if (i==curzone->kids.n || direction<0) i--;
		if (i<0) {
			current=NULL;
			current_image_index=-1;
			if (curzone->kids.n==0) {
				cerr <<"No more images!"<<endl;
				exit(0);
			}
			return 3;
		}
	}
	current_image_index=i;

	 //change window name
	char newname[10+strlen(current->filename)];
	sprintf(newname,"%s (Liv)",current->filename);
	WindowTitle(newname);
	
	PositionSelectionBoxes();
	PositionTagBoxes();
	
	if (!(current->state&FILE_Has_exif)) current->fillinfo(FILE_Has_exif);

	return 0;
}

//void LivWindow::PositionMenuBoxes()
//{
//All:
//	Sort:
//	  Date
//	  File Size
//	  Pixel Size
//	  Width
//	  Height
//	  Random
//	  Reverse order
//  Filter:
//    Tags, 1.any with, 2.must have, 3.must not have
//	  Date range
//	  pixel size range
//	  file size range
//}

void LivWindow::PositionMiscBoxes()
{ ***
	for (int c=0; c<actions->n; c++) {
		if (actions->e[c]->action==LIVA_Menu) {
			int w=getextent(_("Menu"),-1,NULL,NULL);
			int th=app->defaultlaxfont->textheight();
			actions->e[c]->minx=win_w-(w+th);
			actions->e[c]->maxx=win_w;
			actions->e[c]->miny=0;
			actions->e[c]->maxy=th*2;
		}
	}
}

//! Flush selboxes, and repopulate.
void LivWindow::PositionSelectionBoxes()
{ ***
	currentactionbox=NULL;
	selboxes.flush();

	char str[100];
	int x,y,w,h;

	if (selection.kids.n==0) {
		sprintf(str,_("Select"));
		getextent(str,-1,&w,&h);
		
		selboxes.push(new ActionBox(str,
									LIVA_Select,0,
									1,
									win_w-w-h, win_w,
									win_h-3.5*h,win_h-2*h,
									1));
		return;
	}

	 //select or deselect current button
	if (current && selection.kids.findindex(current)>=0) sprintf(str,_("Deselect"));
	else sprintf(str,_("Select"));
	getextent(str,-1,&w,&h);
	selboxes.push(new ActionBox(str,
								LIVA_Select,0,
								1,
								win_w-w-h, win_w,
								win_h-3.5*h,win_h-2*h,
								1));


	 //show all selected button
	sprintf(str,"%d selected",selection.kids.n);
	getextent(str,-1,&w,&h);
	x=win_w-w-h;
	y=win_h-h*2;
	selboxes.push(new ActionBox(str,
								LIVA_Show_All_Selected,0,
								1,
								win_w-w-h, win_w,
								win_h-2*h,win_h));

	 //put small thumbnails next to text
	ImageFile *img;
	int ww,hh;
	double s;
	for (int c=0; c<selection.kids.n && x>0; c++) {
		img=selection.kids.e[c]->image;
		sprintf(str,"img %d",c);

		ww=img->width;
		hh=img->height;
		if (ww<=0 || hh<=0) continue;

		s=(2.*h)/hh;
		ww*=s;
		selboxes.push(new ActionBox(str,
									LIVA_Show_Selected, c,
									1,
									x-ww, x,
									y,y+2*h));

		x-=ww;
	}
}

//! Flush tagboxes, and repopulate.
void LivWindow::PositionTagBoxes()
{
	if (!current) return;
	int x=0, y, starty=(SHOW_NUM_SINGLE_LINE+1)*app->defaultlaxfont->textheight();
	y=starty;

	tagboxes.flush();
	int textheight=app->defaultlaxfont->textheight();
	int w,maxw=0;
	int colstart=0;
	for (int c=0; c<current->NumberOfTags(); c++) {
		//ActionBox(const char *t, int a, int isabsdims, double x1, double x2, double y1, double y2);
		w=textheight+getextent(current->GetTag(c),-1,NULL,NULL);
		if (w>maxw) maxw=w;
		tagboxes.push(new ActionBox(current->GetTag(c),
									LIVA_EditTag,c,
									1,
									x,x+w,
									y,y+textheight));
		y+=textheight;
		if (y>win_h-textheight && c>colstart) {
			 //start a new column when too many tags in old column
			for (int c2=colstart; c2<tagboxes.n; c2++) tagboxes.e[c2]->maxx=x+maxw;
			colstart=c+1;
			y=starty;
			x+=maxw;
		}
	}

	if (newtag) {
		newtag->minx=x;
		newtag->maxx=x+getextent(newtag->text.c_str(),-1,NULL,NULL);
		newtag->miny=y;
		newtag->maxy=y+textheight;
	}
}


//------------------------- files maintenance helpers

typedef int (*SortFunc)(const void *, const void *);

int dateCompare(const void *v1, const void *v2)
{
	ImageFile *const *img1=static_cast<ImageThumb*const*>(v1)->image;
	ImageFile *const *img2=static_cast<ImageThumb*const*>(v2)->image;

	if ((*img1)->fileinfo.st_mtime==(*img2)->fileinfo.st_mtime) return 0;
	if ((*img1)->fileinfo.st_mtime<(*img2)->fileinfo.st_mtime) return -1;
	return 1;
}

int nameCompare(const void *v1, const void *v2)
{
	ImageFile *const *img1=static_cast<ImageThumb*const*>(v1)->image;
	ImageFile *const *img2=static_cast<ImageThumb*const*>(v2)->image;

	return strcmp((*img1)->filename,(*img2)->filename);
}

int sizeCompare(const void *v1, const void *v2)
{
	ImageFile *const *img1=static_cast<ImageThumb*const*>(v1)->image;
	ImageFile *const *img2=static_cast<ImageThumb*const*>(v2)->image;

	if ((*img1)->fileinfo.st_size==(*img2)->fileinfo.st_size) return 0;
	if ((*img1)->fileinfo.st_size<(*img2)->fileinfo.st_size) return -1;
	return 1;
}

int pixelsCompare(const void *v1, const void *v2)
{
	ImageFile *const *img1=static_cast<ImageThumb*const*>(v1)->image;
	ImageFile *const *img2=static_cast<ImageThumb*const*>(v2)->image;

	int a1=(*img1)->width*(*img1)->height, a2=(*img2)->width*(*img2)->height;
	if (a1==a2) return 0;
	if (a1<a2) return -1;
	return 1;
}

int widthCompare(const void *v1, const void *v2)
{
	ImageFile *const *img1=static_cast<ImageThumb*const*>(v1)->image;
	ImageFile *const *img2=static_cast<ImageThumb*const*>(v2)->image;

	if ((*img1)->width==(*img2)->width) return 0;
	if ((*img1)->width<(*img2)->width) return -1;
	return 1;
}

int heightCompare(const void *v1, const void *v2)
{
	ImageFile *const *img1=static_cast<ImageThumb*const*>(v1)->image;
	ImageFile *const *img2=static_cast<ImageThumb*const*>(v2)->image;

	if ((*img1)->height==(*img2)->height) return 0;
	if ((*img1)->height<(*img2)->height) return -1;
	return 1;
}


/*! sortby is a comma separated list of terms to sort by.
 * Currently, it can be some combination of:
 *  date, size, name, width, height, random, pixels.
 *
 * If sortby==NULL, then nothing is done.
 *
 * Return 0 for success, or nonzero for some kind of error, and files not sorted.
 *
 * \todo only the 1st sort parameter is used.
 */
int LivWindow::Sort(const char *sortby)
{
	if (!sortby) return 0;
	int n=0;
	char **strs=split(sortby,',', &n);
	if (n==0) return 1;
	for (int c=0; c<n; c++) stripws(strs[c]);


	char *local=NULL;
	int nn=0;
	ImageThumb **array=curzone->kids.extractArrays(&local,&nn);

	SortFunc func=NULL;
	if (!strcasecmp(strs[0],"date")) func=dateCompare;
	else if (!strcasecmp(strs[0],"size")) func=sizeCompare;
	else if (!strcasecmp(strs[0],"name")) func=nameCompare;
	else if (!strcasecmp(strs[0],"pixels")) func=pixelsCompare;
	else if (!strcasecmp(strs[0],"width")) func=widthCompare;
	else if (!strcasecmp(strs[0],"height")) func=heightCompare;
	else if (!strcasecmp(strs[0],"random")) {
		int s; //place to swap
		ImageFile *t;
		for (int c=nn-1; c>0; c--) {
			s=((double)random()/RAND_MAX)*c;
			t=array[c];
			array[c]=array[s];
			array[s]=t;
		}
		func=NULL;
	}

	if (func) qsort(static_cast<void*>(array), nn, sizeof(ImageThumb*), func); 

	curzone->kids.insertArrays(array,local,nn);
	MapThumbs();
	needtodraw=1;

	deletestrs(strs,n);
	return 0;
}

//! Reverse the order of files.
void LivWindow::ReverseOrder()
{
	if (!curzone->kids.n) return;

	char *local=NULL;
	int nn=0;
	ImageThumb **array=curzone->kids.extractArrays(&local,&nn);

	int s;
	ImageFile *t;
	for (int c=0; c<nn/2; c++) {
		s=nn-1-c;
		t=array[c];
		array[c]=array[s];
		array[s]=t;
	}

	curzone->kids.insertArrays(array,local,nn);
	MapThumbs();
	needtodraw=1;
}

/*! which==1 for collection,
 * which==2 for selection,
 * which==0 for curzone.
 */
int LivWindow::NumFiles(int which)
{
	if (which==0) return curzone->kids.n;
	if (which==1) return collection.kids.n;
	if (which==2) return selection.kids.n;

	return 0;
}

//! Remove image index (in current zone) from current zone.
int LivWindow::RemoveFile(int index)
{
	ImageFile *img=NULL;
	if (index<0 || index>=curzone->kids.n) return 1;
	img=curzone->kids.pop(index);
	curzone->kids.remove(index);
	tagcloud.RemoveObject(img);
	img->dec_count();
	MapThumbs();
	return 0;
}

/*! Returns 1 if not a directory.
 */
int LivWindow::AddDirectory(const char *dir, int as_set, const char *tags)
{ ***
	if (file_exists(dir,1,NULL)!=S_IFDIR) return 1;
	
	return 0;
}

/*! Add to files stack, and reference it from list. If list==NULL, add to main collection.
 *
 * Return the number of files added.
 *
 * \todo implement recursive
 */
int LivWindow::AddFile(const char *file, const char *tags, ImageThumb *list)
{ ***
	if (isblank(file)) return 0;
	int n=0;
	if (list==NULL) list=&collection;

	ImageFile *img=new ImageFile(file);
	img->InsertTags(tags,0);
	int recurse=0; //***

	if (file_exists(img->filename,1,NULL)==S_IFDIR) {
		if (recurse) {
			delete img; img=NULL;
			DIR *dir=opendir(file);
			if (!dir) {
				DBG cout << "*** could not open presumed directory: "<< file<<endl;
				return n;
			}
			char *str=NULL;
			struct dirent *entry;
			do { //readdir scandir telldir
				entry=readdir(dir);
				if (!entry) break;

				//if (!strcmp(entry.d_name,".") || !strcmp(entry.d_name,"..")) continue;
				if (!strcmp(entry->d_name,".")) continue;

				makestr(str,file); //start with the dir name
				if (file[strlen(file)]!='/') appendstr(str,"/");
				appendstr(str,entry->d_name); //append file name
				img=new ImageFile(str);
				if (file_exists(img->filename,1,NULL)==S_IFREG) {
					list->push(img);
					tagcloud.AddObject(img);
					img->dec_count();
					n++;
				} else {
					delete img; img=NULL;
				}
			} while (entry);
			delete[] str;
			closedir(dir);

		} else { //no recurse
			img=new ImageFile(file);
			img->is_set=SET_IS_DIRECTORY;
		}

	}
	
	 //add to full list of files, sorting by file name
	int lower=0,upper=files.n-1, mid, c;
	if (files.n) {
		do {
			c=strcmp(img->filename,files.e[lower]->filename);
			if (c==0) break; //found!
			if (c<0) {
				files.push(img,-1,lower);
				break;
			} else {
				c=strcmp(img->filename,files.e[upper]->filename);
				if (c==0) break; //found!
				if (c>0) files.push(img,-1,upper+1);
				break;
			}
			mid=(upper+lower)/2;
			c=strcmp(img->filename,files.e[mid]);
			if (c==0) break;
			if (c<0) { upper=mid; }
			else { lower=mid+1; }
			if (lower==upper) { files.push(img,-1,lower); break; }
		} while (lower<upper);
	} else files->push(img);

	collection.Add(img);
	tagcloud.AddObject(img);
	img->dec_count();
	n++;

	needtomap=1;
	return n;
}

} //namespace Liv


