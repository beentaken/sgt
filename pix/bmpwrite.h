/* ----------------------------------------------------------------------
 * Declarations for functions to write out .BMP files.
 */

#ifndef BMPWRITE_H
#define BMPWRITE_H

enum { BMP, PPM };

struct Bitmap;
struct Bitmap *bmpinit(char const *filename, int width, int height, int type);
void bmppixel(struct Bitmap *bm,
	      unsigned char r, unsigned char g, unsigned char b);
void bmpendrow(struct Bitmap *bm);
void bmpclose(struct Bitmap *bm);

#endif /* BMPWRITE_H */
