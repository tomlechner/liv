
LIV
===
Laxkit Image Viewer  
Version 0.1  
by Tom Lechner, 2008  


ABOUT
-----
A work in progress... in fact so in progress that it doesn't work yet!

Liv is a very simple Imlib2 based image viewer, approximately in the same spirit
as feh. Like feh, liv is all about showing images either singly or as a slide show,
without the horrible interface clutter common in so many image viewers.

Unlike feh, liv is supposed to provide better zooming capabilities,
remembers zoom settings per image as you flip through them, can display exif
information, and uses and can generate thumbnails in the freedesktop.org 
manner (this is not quite programmed yet!!). Grouping mechanisms are planned.


COMPILING
---------
You will need to be able to compile the Laxkit (https://github.com/Laidout/laxkit).
After that, you should be able to compile easily with `make`.

Making will create src/liv, which currently runs totally independently, so you can
simply move it to ~/bin, /usr/local/bin or whereever you want.

