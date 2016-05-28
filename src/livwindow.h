// $Id: livwindow.h 635 2012-03-12 00:21:48Z tomlechner $


#ifndef LIV_LIVWINDOW_H
#define LIV_LIVWINDOW_H

#include <lax/anxapp.h>
#include <lax/doublebbox.h>
#include <lax/refcounted.h>
#include <lax/tagged.h>
#include <lax/doublebbox.h>
#include <lax/buttondowninfo.h>
#include <lax/freedesktop.h>
#include <lax/transformmath.h>
#include <lax/laximages.h>
#include <lax/laximlib.h>

#include <string>

namespace Liv {

//------------------------------ ActionBox ------------------------------------------


enum ActionClassType {
	ACTIONCLASS_Display,
	ACTIONCLASS_Trigger,
	ACTIONCLASS_Toggle,
	ACTIONCLASS_Pan,
	ACTIONCLASS_VerticalPan,
	ACTIONCLASS_Slider,
	ACTIONCLASS_VerticalSlider,
};


class ActionBox : public Laxkit::DoubleBBox
{
 public:
	std::string text;
	int action;
	int index; //kind of a sub-action, action might be Show_Selected, and index would be which selected to show
	ActionClassType action_class;
	double v;
	int is_abs_dims; //2 is real coords, 1 screen coords, 0 is 0..1 space
	int show_hover; //show a highlighted box when the mouse hovers over the box
	int mode, submode; //states under which box should be used
	ActionBox();
	ActionBox(const char *txt,
			  int a,
			  int i,
			  int isabsdims,
			  double x1, double x2, double y1, double y2,
			  int shover=0,
			  int mde=0);

	int GetInt();
	double GetDouble();
};



//----------------------------- class ImageThumb ------------------------------------

class ImageFile;

class ImageThumb : public Laxkit::RefCounted
{
  public:
	ImageFile *image;

	ImageThumb *parent;
	double scale_to_kids;
	double x,y; //offset to parent's kid offset
	int width,height; //dimensions in parent coordinates
	int kidx,kidy;   //offset to parents
	int kidswidth,kidsheight;//kid space dimensions
	int gap;

	Laxkit::RefPtrStack<ImageThumb> kids;

	ImageThumb();
	ImageThumb(ImageFile *img, int xx,int yy);
	virtual ~ImageThumb();
	virtual void Set(ImageFile *img, int xx,int yy);
	virtual void Set(int xx,int yy, int ww,int hh);
	virtual int Gap(int newgap);
	virtual int Add(ImageThumb *thumb);
	virtual int Add(ImageFile *img);
	virtual void Layout(int how);
};



//----------------------------- class ImageFile --------------------------------------
enum LivSettype {
	ITEM_Is_File,
	ITEM_Is_Set,
	ITEM_Is_Directory,

	ITEM_MAX
};

enum PreviewState {
	PREVIEW_Doesnt_Exist,
	PREVIEW_Exists_Not_Loaded,
	PREVIEW_Loading,
	PREVIEW_Loaded
};


class ImageFile : public Laxkit::RefCounted, public Laxkit::Tagged
{
 public:
	int type;   //whether it has kids. 1 is simple set, 2 is directory. if files removed from it, it becomes normal set
	int filetype; //if file is image, text, unknown, etc
	int is_set;

	//RefPtrStack<ImageFile> alts; //Any different formats of an image that should be considered the same image
	Laxkit::RefPtrStack<ImageFile> kids; //for sets or directories, this is its contents
	Laxkit::RefPtrStack<ImageFile> sets; //all sets this image belongs to
	ImageFile *parent;

	int mark; //for marked sets, this gets toggled

	char *name;
	char *filename;
	char *title;
	char *description;
	flatpoint metapoint;
	LaxFiles::Attribute *meta;

	double matrix[6];  //matrix for normal view
	int width, height; //pixel size

	Laxkit::LaxImage *image;
	struct stat fileinfo;
	int state;    //how much of the file's info has been found

	char *preview;
	int pwidth,pheight; //preview pixel size
	PreviewState preview_state;
	clock_t lastviewtime;

	ImageFile();
	ImageFile(const char *fname);
	ImageFile(int ntype, const char *nname, const char *nfilename, const char *ntitle, const char *ndesc,LaxFiles::Attribute *nmeta);
	virtual ~ImageFile();
	virtual int fillinfo(int which);
	virtual const char *whattype() { return "ImageFile"; }
};



//------------------------------ LivWindow ----------------------------------------

enum LivWindowActions {
	LIVA_None,

	LIVA_Previous,
	LIVA_Next,
	LIVA_Play,
	LIVA_Pause,
	LIVA_Beginning,
	LIVA_End,
	LIVA_Up,
	LIVA_Scale_1_to_1,
	LIVA_Fit_To_Screen,
	LIVA_Center,
	LIVA_NewTag,
	LIVA_EditTag,
	LIVA_Select,
	LIVA_Remove,
	LIVA_RotateScreen,
	LIVA_RotateScreenR,
	LIVA_RotateImage,
	LIVA_RotateImageR,
	LIVA_ToggleMeta,
	LIVA_ToggleInfo,
	LIVA_ZoomIn,
	LIVA_ZoomOut,
	LIVA_ToggleFullscreen,
	LIVA_ReplaceWithSelected,
	LIVA_Show_Selected_Image,
	LIVA_Show_All_Selected,
	LIVA_ToggleBrowse,
	LIVA_RemapThumbs,

	LIVA_SaveCollection,

	LIVA_Menu,
	LIVA_Sort_Reverse,
	LIVA_Sort_Date,
	LIVA_Sort_Filesize,
	LIVA_Sort_Area,
	LIVA_Sort_Width,
	LIVA_Sort_Height,
	LIVA_Sort_Filename,
	LIVA_Sort_FilenameCaseless,
	LIVA_Sort_Random,

	LIVA_Verbose,
	LIVA_Help,
	LIVA_Quit,
	LIVA_MAX
};

//see ImageFile::fillinfo()
enum ImgLoadState {
	FILE_Not_accessed=0,
	FILE_Has_stat    =1,
	FILE_Has_exif    =2,
	FILE_Has_image   =4
};

//see ImageFile::filetype
enum ImgFileType {
	FILE_is_unknown,
	FILE_is_text,
	FILE_is_dir,
	FILE_is_image,
	FILE_is_binary
};

 //for LivWindow::zoommode
enum ZoomMode {
	LIVZOOM_One_To_One,
	LIVZOOM_Scale_To_Screen,
	LIVZOOM_Shrink_To_Screen
};

 //for LivWindow::viewmode
enum Viewmode {
	VIEW_Any,
	VIEW_Normal,
	VIEW_Help,
	VIEW_Thumbs,
	VIEW_Slideshow
};

//see LivWindow::showbasics
enum LivShowBasics {
	SHOW_Filename   =(1<<0),
	SHOW_Index      =(1<<1),
	SHOW_Filesize   =(1<<2),
	SHOW_Dims       =(1<<3),

	SHOW_Tags       =(1<<4),
	SHOW_MAX        =(1<<5),
	SHOW_All        =(0xffff)
};

enum LivFlags {
	LIV_Autoremove=(1<<0)
};

class LivWindow : public Laxkit::anXWindow
{
  protected:
	Laxkit::ShortcutHandler *sc;
	void InitializePlacements();

  public:
	//RefPtrStack<ImageFile> view;

	Laxkit::RefPtrStack<ImageFile> files; //total list of files, can be arranged in different sets

	 //3 main zones:
	ImageThumb top;
	ImageThumb collection;
	ImageThumb filesystem;

	ImageThumb selection;
	ImageThumb *curzone;

	int defaultwidth;
	int defaultheight;

	int currentmark; //usually 1, but maybe different marks (1 per bit)
	int viewmarked; //in thumb view and image selection, use a subset of files with this mark mask

	ImageFile *current;
	int current_image_index; //index in current set
	char *collectionfile;
	Laxkit::TagCloud tagcloud;


	unsigned long livflags; //see LivFlags
	Laxkit::ButtonDownInfo buttondown;
	int device1,device2;
	flatpoint lbdown,mbdown;
	int mousemoved;
	int mx,my;
	int showoverlay;
	int showbasics;
	int showmeta;
	int showmarkedpanel;
	int imagesonly;
	int thumb_location;
	int dirsets; //0 is load dir contents and not keep as a set, 1 load as set
	int firsttime;
	int zoommode; //1==scale to screen, 2==scale to screen if bigger, 0==exact size
	int viewmode, lastmode;
	int previewsize;
	int isonetoone;
	int fullscreen;
	int verbose;
	int lastviewjump;//whether zoomed into an image, or just jumped right to it
	int overlayalpha; //default overlay transparency
	int screen_rotation;
	double screen_matrix[6]; //screen rotation fix
	double thumb_matrix[6];  //transform to curzone
	double thumbdisplaywidth;//pixel width to fit thumbnails to
	int needtomap; //whether the thumb positions need to be reset
	double thumbgap; //this is pixel border around images in thumb view

	ActionBox *currentactionbox;
	int hover_image;
	Laxkit::DoubleBBox hover_area;
	char *hover_text;

	int timerid;
	int slidedelay;//in milliseconds

	Laxkit::PtrStack<ActionBox> *actions;
	Laxkit::PtrStack<ActionBox> menuactions;
	Laxkit::PtrStack<ActionBox> imageactions;
	Laxkit::PtrStack<ActionBox> selboxes;
	Laxkit::PtrStack<ActionBox> tagboxes;
	ActionBox *newtag;
	int first_tag_action;


	virtual void Refresh();
	virtual void RefreshHelp();
	virtual void RefreshNormal();
	virtual void RefreshThumbs();

	virtual void DrawThumbsRecurseUp  (ImageThumb *thumb, double *m);
	virtual void DrawThumbsRecurseDown(ImageThumb *thumb, double *m);

	 //event dispatching functions
	LivWindow(anXWindow *parnt,const char *nname,const char *ntitle,unsigned long nstyle,
				                int xx,int yy,int ww,int hh,int brder,
								int zoom,int fs,int slided,
								int bgr=0, int bgg=0, int bgb=0,
								int memorythumbs=1);
	virtual ~LivWindow();
	virtual int Event(const Laxkit::EventData *data,const char *mes);
	virtual int LBDown(int x,int y,unsigned int state,int count, const Laxkit::LaxMouse *d);
	virtual int LBUp(int x,int y,unsigned int state, const Laxkit::LaxMouse *d);
	virtual int MBDown(int x,int y,unsigned int state,int count, const Laxkit::LaxMouse *d);
	virtual int MBUp(int x,int y,unsigned int state, const Laxkit::LaxMouse *d);
	virtual int RBDown(int x,int y,unsigned int state,int count, const Laxkit::LaxMouse *d);
	virtual int RBUp(int x,int y,unsigned int state, const Laxkit::LaxMouse *d);
	virtual int WheelUp(int x,int y,unsigned int state, int count, const Laxkit::LaxMouse *d);
	virtual int WheelDown(int x,int y,unsigned int state, int count, const Laxkit::LaxMouse *d);
	virtual int MouseMove(int x,int y,unsigned int state, const Laxkit::LaxMouse *d);
	virtual int CharInput(unsigned int ch, const char *buffer,int len,unsigned int state, const Laxkit::LaxKeyboard *d);
	virtual int MoveResize(int nx,int ny,int nw,int nh);
	virtual int Resize(int nw,int nh);
	virtual int Idle(int tid);
	virtual int init();
	virtual Laxkit::ShortcutHandler *GetShortcuts();
	virtual int PerformAction(int action);

	virtual int Mode(int newmode);
	virtual void RotateScreen(int howmuch);
	virtual void InitActions();
	virtual int toobig();
	virtual void setzoom(ImageThumb *which=NULL);
	virtual void ScaleToFit(ImageFile *img);
	virtual void Zoom(flatpoint center,double amount);
	virtual ActionBox *GetAction(int x,int y,unsigned int state, int *boxindex=NULL);
	virtual ActionBox *GetAction(Laxkit::PtrStack<ActionBox> *alist, int x,int y,unsigned int state, int *boxindex);
	virtual int SelectImage(int i);
	virtual ImageThumb * findImageAtCoord(int x,int y, int *index_in_parent);
	virtual void PositionMiscBoxes();
	virtual void PositionTagBoxes();
	virtual void PositionSelectionBoxes();
	virtual int MapThumbs();
	virtual void ShowMarkedPanel();
	virtual int ToggleMenu();
	virtual int StartSlideshow();

	virtual int SaveCollection(const char *file, ImageThumb *list);
	virtual void dump_img(FILE *f, ImageThumb *t, int indent);
	virtual int LoadCollection(const char *file);

	virtual int AddDirectory(const char *dir, int as_set, const char *tags);
	virtual int AddFile(const char *file, const char *tags, ImageThumb *list);
	virtual int RemoveFile(int index);
	virtual int NumFiles(int which=0);

	virtual void ReverseOrder();
	virtual int Sort(const char *sortby);
};



} //namespace Liv

#endif


