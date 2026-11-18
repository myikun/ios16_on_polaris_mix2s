/*
 * topaz_test: the same ground gl_test covers, but loud, and it explains its own crash.
 *
 * Two things learned the hard way:
 *   - launchd sends stdout to a file, stdio buffers it, and a SIGKILL throws the
 *     buffer away, which is why gl_test died without a single line. stdout is
 *     unbuffered here and every step is printed before and after.
 *   - after the buffers are mapped, the crash is inside Mesa, and a bare pc tells us
 *     nothing. So the handlers below resolve every frame with dladdr and print
 *     "image+offset (symbol)", which is enough to read the name off libOSMesa's
 *     symbol table.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/ucontext.h>
#include <syslog.h>

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

#define OSMESA_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_MAX_TEXTURE_SIZE 0x0D33

/*
 * stdout goes to /dev/console, which does not always reach the kernel's console
 * ring; syslog always does. Every line goes to both.
 */
static void
say(const char *what)
{
	printf("topaz_test: %s\n", what);
	syslog(LOG_NOTICE, "topaz_test: %s", what);
}

static void
note(const char *fmt, int a, int b, int c, int d)
{
	char line[128];

	snprintf(line, sizeof(line), fmt, a, b, c, d);
	printf("topaz_test: %s\n", line);
	syslog(LOG_NOTICE, "topaz_test: %s", line);
}

static void
where(const char *tag, void *addr)
{
	Dl_info info;

	if (dladdr(addr, &info) && info.dli_fname != NULL) {
		printf("topaz_test:   %s %p = %s+0x%lx (%s)\n", tag, addr, info.dli_fname,
		    (unsigned long)((char *)addr - (char *)info.dli_fbase),
		    info.dli_sname != NULL ? info.dli_sname : "?");
	} else {
		printf("topaz_test:   %s %p (no image)\n", tag, addr);
	}
}

static void
on_signal(int sig, siginfo_t *si, void *context)
{
	void *frames[32];
	int n;

	printf("\ntopaz_test: signal %d at %p\n", sig, si != NULL ? si->si_addr : NULL);
	if (context != NULL) {
		ucontext_t *uc = (ucontext_t *)context;
		where("pc", (void *)uc->uc_mcontext->__ss.__pc);
		where("lr", (void *)uc->uc_mcontext->__ss.__lr);
	}
	n = backtrace(frames, 32);
	for (int i = 0; i < n; i++) {
		where("bt", frames[i]);
	}
	fflush(stdout);
	_exit(70);
}

static void
crash_handlers(void)
{
	struct sigaction sa;
	int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP };

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_signal;
	sa.sa_flags = SA_SIGINFO;
	for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
		sigaction(sigs[i], &sa, NULL);
	}
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
	crash_handlers();

	say("start");
	if (argc > 2) {
		width = atoi(argv[1]);
		height = atoi(argv[2]);
	}
	note("target %d x %d", width, height, 0, 0);

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

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &one);
	note("GL_MAX_TEXTURE_SIZE %d", one, 0, 0, 0);

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
	note("centre pixel %02x %02x %02x %02x", pixels[0], pixels[1], pixels[2], pixels[3]);

	say("OSMesaDestroyContext");
	OSMesaDestroyContext(ctx);
	say("done");

	return 0;
}

