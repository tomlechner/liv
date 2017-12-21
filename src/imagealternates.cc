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

#include "imagealternates.h"


using namespace Laxkit;

/*! \typedef int ImageAlternateCreationFunc(const char *orig_file,char **created_file,ImageAlternateSpec *spec)
 * \brief Used from ImageAlternateSpec to create alternates for images.
 *
 * Return 0 for success, or nonzero error which means to alt created.
 */

/*! \class ImageAlternateSpec
 * \brief Classs to define strategy to make alternate forms of images.
 * 
 * \todo laidout should have stack of default alternates, but import image should
 *    be able to override the defaults
 */

ImageAlternateSpec::ImageAlternateSpec()
{
	alternateid=0;
	alternate=NULL;
	alttype=NULL;
	defaultbasename=NULL;
	maxwidth=0;
	autothreshhold=0;
	create=NULL;
}

ImageAlternateSpec::~ImageAlternateSpec()
{
	if (alternateid) delete[] alternateid;
	if (alttype) delete[] location;
	if (defaultbasename) delete[] defaultbasename;
}


------------------------
int WebSizeCreationFunc(const char *orig_file,char **created_file,ImageAlternateSpec *spec)
{
	return generate_preview(orig_file, to_file,"jpg",spec->maxwidth,spec->maxwidth,1);
}

//----------------------------------- ImageAlternate ---------------------------------
/*! \class ImageAlternate
 * \brief Info about an alternate image
 */

ImageAlternate::ImageAlternate(const char *file, const char *alt)
{
	filename=newstr(file);
	alttags=newstr(alt);
	width=height=0;
}

ImageAlternate::~ImageAlternate()
{
	if (filename) delete[] filename;
	if (alttags) delete[] alttags;
}




