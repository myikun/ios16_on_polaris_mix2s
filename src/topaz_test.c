/*
 * topaz_test: the same ground gl_test covers, but loud.
 *
 * gl_test died without a single line of output: launchd sends its stdout to a file,
 * stdio buffers that, and a SIGKILL throws the buffer away. This program sets stdout
 * unbuffered and prints before and after every step, so the log shows exactly which
 * call the GPU stack fails on.
 *
 * Built in the userland workflow against the libOSMesa that workflow produces:
 *   clang -target arm64-apple-ios16.0 -isysroot $SDK -O1 \
 *       -framework CoreFoundation -framework IOKit libOSMesa.8.dylib \
 *       -o topaz_test topaz_test.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OSMesa's ES entry points, as eagl.c and gl_test declare them */
typedef void *OSMesaContext;
extern OSMesaContext OSMesaCreateContextExt(unsigned format, int depth, int stencil,
    int accum, OSMesaContext share);
extern int OSMesaMakeCurrent(OSMesaContext ctx, void *buffer, unsigned type,
    int width, int height);
extern void OSMesaDestroyContext(OSMesaContext ctx);
extern const char *OSMesaGetProcAddress(const char *name);

extern void glClearColor(float r, float g, float b, float a);
extern void glClear(unsigned mask);
extern void glFinish(void);
extern void glReadPixels(int x, int y, int width, int height, unsigned format,
    unsigned type, void *pixels);
extern void glGetIntegerv(unsigned pname, int *params);
extern void glViewport(int x, int y, int w, int h);
extern void glEnable(unsigned cap);
extern void glDisable(unsigned cap);

#define OSMESA_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

static void
say(const char *what)
{
	printf("topaz_test: %s\n", what);
	fflush(stdout);
}

int
main(int argc, char **argv)
{
	int width = 256, height = 256;
	unsigned char *pixels;
	OSMesaContext ctx;
	int one = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	say("start");
	if (argc > 2) {
		width = atoi(argv[1]);
		height = atoi(argv[2]);
	}
	printf("topaz_test: target %dx%d\n", width, height);
	fflush(stdout);

	say("OSMesaCreateContextExt");
	ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
	if (ctx == NULL) {
		say("context is NULL -- the GPU pipe could not be created");
		return 1;
	}
	say("context created");

	pixels = (unsigned char *)malloc((size_t)width * height * 4);
	if (pixels == NULL) {
		say("no memory for the framebuffer");
		return 1;
	}
	memset(pixels, 0x80, (size_t)width * height * 4);

	say("OSMesaMakeCurrent");
	if (!OSMesaMakeCurrent(ctx, pixels, GL_UNSIGNED_BYTE, width, height)) {
		say("MakeCurrent failed");
		return 1;
	}
	say("current");

	if (OSMesaGetProcAddress("glGetIntegerv") != NULL) {
		say("query GL_RENDERER");
		/* no glGetString here: eagl owns the string table; the numbers are enough */
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &one);
		printf("topaz_test: GL_MAX_TEXTURE_SIZE %d\n", one);
		fflush(stdout);
	}

	say("glViewport");
	glViewport(0, 0, width, height);

	say("glClear");
	glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	say("glFinish (waits for the GPU)");
	glFinish();
	say("finished");

	say("glReadPixels");
	glReadPixels(width / 2, height / 2, 1, 1, OSMESA_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
	printf("topaz_test: centre pixel %02x %02x %02x %02x\n",
	    pixels[0], pixels[1], pixels[2], pixels[3]);
	fflush(stdout);

	say("OSMesaDestroyContext");
	OSMesaDestroyContext(ctx);
	say("done");

	return 0;
}
