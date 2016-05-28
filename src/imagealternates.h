//
// $Id$
//	
// Laidout, for laying out
// Please consult http://www.laidout.org about where to send any
// correspondence about this software.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// For more details, consult the COPYING file in the top directory.
//
// Copyright (c) 2012 Tom Lechner
//
#ifndef IMAGEALTERNATES_H
#define IMAGEALTERNATES_H



typedef int ImageAlternateCreationFunc(const char *orig_file,char **created_file,ImageAlternateSpec *spec);

class ImageAlternateSpec : public Laxkit::RefCounted
{
 public:
	unsigned long alternateid;

	char *alttype; //hint for general location: fd-large, fd-normal, laidout-cache, scribus-cache, custom
	char *alternate; //thumb, screen, print, color-corrected-screen
	char *defaultbasename; //for instance ~/.thumbnails/large/@.png

	int maxwidth; //generated alternates must be below this width/height
	int minwidth; //at least one dimension must be at least this wide (or tall)
	int autothreshhold; //create this kind of alternate automatically for images over this pixel area size
	ImageAlternateCreationFunc *create;

	ImageAlternateSpec();
	virtual ~ImageAlternateSpec();
};

class ImageAlternate : public Laxkit::Tagged
{
  public:
	char *filename;
	int width,height;

	ImageAlternate(const char *file, int w,int h);
	virtual ~ImageAlternate();
};

ImageAlternate::ImageAlternate(const char *file, int w,int h)
{
	filename=newstr(file);
	width=w;
	height=h;
}

ImageAlternate::~ImageAlternate()
{
	if (filename) delete[] filename;
}

------------
class LaxImageWithAlts : public Laxkit::LaxImage
{
  public:
	LaxImageWithAlts();
	virtual ~LaxImageWithAlts();
	RefPtrStack<ImageAlternate> alts;
};

#endif

