/* bytesnap - minimal X11 screenshot utility */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int
bitpos(unsigned long mask)
{
	if (!mask) return 0;
	for (int i = 0; i < (int)(sizeof(mask) * 8); i++)
		if (mask & (1UL << i)) return i;
	return 0;
}

int
main(void)
{
	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "bytesnap: could not open display\n");
		return 1;
	}

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	int w = DisplayWidth(dpy, screen);
	int h = DisplayHeight(dpy, screen);

	XImage *img = XGetImage(dpy, root, 0, 0, w, h, AllPlanes, ZPixmap);
	if (!img) {
		fprintf(stderr, "bytesnap: XGetImage failed\n");
		XCloseDisplay(dpy);
		return 1;
	}

	/* save as PPM (simple, no extra deps) */
	char *home = getenv("HOME");
	char path[512];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	if (home)
		snprintf(path, sizeof(path), "%s/screenshots", home);
	else
		snprintf(path, sizeof(path), "/tmp/screenshots");

	mkdir(path, 0755);

	char filename[64];
	strftime(filename, sizeof(filename), "%Y%m%d_%H%M%S", tm);
	char fullpath[1024];
	snprintf(fullpath, sizeof(fullpath), "%s/%s.ppm", path, filename);

	FILE *fp = fopen(fullpath, "wb");
	if (!fp) {
		fprintf(stderr, "bytesnap: could not write %s\n", fullpath);
		XDestroyImage(img);
		XCloseDisplay(dpy);
		return 1;
	}

	fprintf(fp, "P6\n%d %d\n255\n", w, h);
	{
		int rshift = bitpos(img->red_mask);
		int gshift = bitpos(img->green_mask);
		int bshift = bitpos(img->blue_mask);
		int rmax = (int)(img->red_mask >> rshift);
		int gmax = (int)(img->green_mask >> gshift);
		int bmax = (int)(img->blue_mask >> bshift);
		if (rmax == 0) rmax = 1;
		if (gmax == 0) gmax = 1;
		if (bmax == 0) bmax = 1;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				unsigned long p = XGetPixel(img, x, y);
				unsigned char r, g, b;
				r = (unsigned char)(((p & img->red_mask) >> rshift) * 255 / rmax);
				g = (unsigned char)(((p & img->green_mask) >> gshift) * 255 / gmax);
				b = (unsigned char)(((p & img->blue_mask) >> bshift) * 255 / bmax);
				fwrite(&r, 1, 1, fp);
				fwrite(&g, 1, 1, fp);
				fwrite(&b, 1, 1, fp);
			}
	}

	fclose(fp);
	XDestroyImage(img);
	XCloseDisplay(dpy);

	printf("%s\n", fullpath);
	return 0;
}
