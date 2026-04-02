/*
 * wine_aspect.m - Enforce 4:3 aspect ratio and scale game content
 * Loaded as reexport wrapper around winemac.so
 */
#include <objc/runtime.h>
#include <objc/message.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

typedef double CGFloat;
typedef struct { CGFloat width, height; } NSSize;
typedef struct { CGFloat x, y; } NSPoint;
typedef struct { NSPoint origin; NSSize size; } NSRect;

/* --- Swizzled implementations --- */

static BOOL swz_preventResizing(id self, SEL _cmd) { return NO; }

typedef void (*AdjustFeaturesIMP)(id, SEL);
static AdjustFeaturesIMP orig_adjustFeatures = NULL;
static void swz_adjustFeaturesForState(id self, SEL _cmd) {
    orig_adjustFeatures(self, _cmd);
    NSSize ratio = {4.0, 3.0};
    ((void(*)(id,SEL,NSSize))objc_msgSend)(self, sel_registerName("setContentAspectRatio:"), ratio);
    NSSize minSize = {640, 480};
    NSSize maxSize = {10000, 10000};
    ((void(*)(id,SEL,NSSize))objc_msgSend)(self, sel_registerName("setContentMinSize:"), minSize);
    ((void(*)(id,SEL,NSSize))objc_msgSend)(self, sel_registerName("setContentMaxSize:"), maxSize);
}

/* Block Wine from resetting window to 640x480 during/after resize */
typedef void (*SetFrameFromWineIMP)(id, SEL, NSRect);
static SetFrameFromWineIMP orig_setFrameFromWine = NULL;
static void swz_setFrameFromWine(id self, SEL _cmd, NSRect contentRect) {
    id title = ((id(*)(id,SEL))objc_msgSend)(self, sel_registerName("title"));
    if (title) {
        const char *t = ((const char*(*)(id,SEL))objc_msgSend)(title, sel_registerName("UTF8String"));
        if (t && strstr(t, "Zoo Tycoon")) {
            if (contentRect.size.width < 650 && contentRect.size.height < 490) {
                int curW = 0, curH = 0;
                FILE *f = fopen("/tmp/zt2_winsize", "r");
                if (f) { fscanf(f, "%d %d", &curW, &curH); fclose(f); }
                if (curW > 660) return;
            }
        }
    }
    orig_setFrameFromWine(self, _cmd, contentRect);
}

/* --- GL scaling via flushBuffer swizzle --- */
typedef void (*FlushBufferIMP)(id, SEL);
static FlushBufferIMP orig_flushBuffer = NULL;

/* OpenGL function pointers (loaded dynamically) */
typedef unsigned int GLenum;
typedef int GLint;
typedef void (*PFN_glGetIntegerv)(GLenum, GLint*);
typedef void (*PFN_glBindFramebuffer)(GLenum, unsigned int);
typedef void (*PFN_glBlitFramebuffer)(int,int,int,int,int,int,int,int,unsigned int,GLenum);
typedef void (*PFN_glGenFramebuffers)(int, unsigned int*);
typedef void (*PFN_glGenTextures)(int, unsigned int*);
typedef void (*PFN_glBindTexture)(GLenum, unsigned int);
typedef void (*PFN_glTexImage2D)(GLenum,int,int,int,int,int,GLenum,GLenum,const void*);
typedef void (*PFN_glTexParameteri)(GLenum,GLenum,int);
typedef void (*PFN_glFramebufferTexture2D)(GLenum,GLenum,GLenum,unsigned int,int);

static PFN_glBlitFramebuffer p_glBlitFramebuffer = NULL;
static PFN_glBindFramebuffer p_glBindFramebuffer = NULL;
static PFN_glGetIntegerv p_glGetIntegerv = NULL;
static int gl_loaded = 0;

static void load_gl(void) {
    extern void *dlsym(void*, const char*);
    #define RTLD_DEFAULT ((void*)-2)
    p_glBlitFramebuffer = (PFN_glBlitFramebuffer)dlsym(RTLD_DEFAULT, "glBlitFramebuffer");
    p_glBindFramebuffer = (PFN_glBindFramebuffer)dlsym(RTLD_DEFAULT, "glBindFramebuffer");
    p_glGetIntegerv = (PFN_glGetIntegerv)dlsym(RTLD_DEFAULT, "glGetIntegerv");
    gl_loaded = 1;
}

static void swz_flushBuffer(id self, SEL _cmd) {
    if (!gl_loaded) load_gl();

    /* Read target size */
    int targetW = 0, targetH = 0;
    { FILE *f = fopen("/tmp/zt2_winsize", "r"); if (f) { fscanf(f, "%d %d", &targetW, &targetH); fclose(f); } }

    if (p_glBlitFramebuffer && p_glBindFramebuffer && p_glGetIntegerv &&
        targetW > 660 && targetH > 500) {
        /* Use temp FBO to avoid read/write overlap on default framebuffer */
        static unsigned int tFBO = 0, tTex = 0;
        static int tW = 0, tH = 0;
        PFN_glGenFramebuffers pGenFBO = (PFN_glGenFramebuffers)dlsym(RTLD_DEFAULT, "glGenFramebuffers");
        PFN_glGenTextures pGenTex = (PFN_glGenTextures)dlsym(RTLD_DEFAULT, "glGenTextures");
        PFN_glBindTexture pBindTex = (PFN_glBindTexture)dlsym(RTLD_DEFAULT, "glBindTexture");
        PFN_glTexImage2D pTexImg = (PFN_glTexImage2D)dlsym(RTLD_DEFAULT, "glTexImage2D");
        PFN_glTexParameteri pTexParam = (PFN_glTexParameteri)dlsym(RTLD_DEFAULT, "glTexParameteri");
        PFN_glFramebufferTexture2D pFBTex = (PFN_glFramebufferTexture2D)dlsym(RTLD_DEFAULT, "glFramebufferTexture2D");

        if (pGenFBO && pGenTex && pBindTex && pTexImg && pFBTex) {
            if (!tFBO) { pGenFBO(1, &tFBO); pGenTex(1, &tTex); }
            if (tW != 640 || tH != 480) {
                pBindTex(0x0DE1/*GL_TEXTURE_2D*/, tTex);
                pTexImg(0x0DE1, 0, 0x8058/*GL_RGBA8*/, 640, 480, 0, 0x1908/*GL_RGBA*/, 0x1401/*GL_UNSIGNED_BYTE*/, 0);
                pTexParam(0x0DE1, 0x2801/*MIN_FILTER*/, 0x2601/*LINEAR*/);
                pTexParam(0x0DE1, 0x2800/*MAG_FILTER*/, 0x2601/*LINEAR*/);
                p_glBindFramebuffer(0x8D40/*GL_FRAMEBUFFER*/, tFBO);
                pFBTex(0x8D40, 0x8CE0/*GL_COLOR_ATTACHMENT0*/, 0x0DE1, tTex, 0);
                tW = 640; tH = 480;
            }
            GLint pR, pD;
            p_glGetIntegerv(0x8CA8, &pR);
            p_glGetIntegerv(0x8CA6, &pD);
            /* Copy 640x480 from default FBO to temp */
            p_glBindFramebuffer(0x8CA8, 0);
            p_glBindFramebuffer(0x8CA9, tFBO);
            p_glBlitFramebuffer(0, 0, 640, 480, 0, 0, 640, 480, 0x4000, 0x2600/*NEAREST*/);
            /* Stretch temp to fill default FBO at window size */
            p_glBindFramebuffer(0x8CA8, tFBO);
            p_glBindFramebuffer(0x8CA9, 0);
            p_glBlitFramebuffer(0, 0, 640, 480, 0, 0, targetW, targetH, 0x4000, 0x2601/*LINEAR*/);
            p_glBindFramebuffer(0x8CA8, pR);
            p_glBindFramebuffer(0x8CA9, pD);
        }
    }

    orig_flushBuffer(self, _cmd);
}

/* --- Aspect thread --- */
static int g_swizzled = 0;

static void *aspect_thread(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < 600; i++) {
        usleep(200000);
        Class WineWindow = objc_getClass("WineWindow");
        if (!WineWindow) continue;

        /* Swizzle once */
        if (!g_swizzled) {
            Method m1 = class_getInstanceMethod(WineWindow, sel_registerName("preventResizing"));
            if (m1) method_setImplementation(m1, (IMP)swz_preventResizing);
            Method m2 = class_getInstanceMethod(WineWindow, sel_registerName("adjustFeaturesForState"));
            if (m2) {
                orig_adjustFeatures = (AdjustFeaturesIMP)method_getImplementation(m2);
                method_setImplementation(m2, (IMP)swz_adjustFeaturesForState);
            }
            Method m3 = class_getInstanceMethod(WineWindow, sel_registerName("setFrameFromWine:"));
            if (m3) {
                orig_setFrameFromWine = (SetFrameFromWineIMP)method_getImplementation(m3);
                method_setImplementation(m3, (IMP)swz_setFrameFromWine);
            }
            /* Swizzle WineOpenGLContext.flushBuffer for GL scaling */
            Class WineGLCtx = objc_getClass("WineOpenGLContext");
            if (WineGLCtx) {
                Method m4 = class_getInstanceMethod(WineGLCtx, sel_registerName("flushBuffer"));
                if (m4) {
                    orig_flushBuffer = (FlushBufferIMP)method_getImplementation(m4);
                    method_setImplementation(m4, (IMP)swz_flushBuffer);
                }
            }
            g_swizzled = 1;
        }

        /* Set aspect ratio and resize subviews on all WineWindows */
        id app = ((id(*)(id,SEL))objc_msgSend)((id)objc_getClass("NSApplication"),
            sel_registerName("sharedApplication"));
        if (!app) continue;
        id windows = ((id(*)(id,SEL))objc_msgSend)(app, sel_registerName("windows"));
        if (!windows) continue;
        unsigned long count = ((unsigned long(*)(id,SEL))objc_msgSend)(windows, sel_registerName("count"));
        unsigned long j;
        for (j = 0; j < count; j++) {
            id win = ((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,
                sel_registerName("objectAtIndex:"), j);
            if (!win || !((BOOL(*)(id,SEL,Class))objc_msgSend)(win,
                sel_registerName("isKindOfClass:"), WineWindow)) continue;

            /* Ensure resizable + aspect ratio + correct content size */
            NSSize ratio = {4.0, 3.0};
            ((void(*)(id,SEL,NSSize))objc_msgSend)(win, sel_registerName("setContentAspectRatio:"), ratio);
            /* If the content area is < 640x480 (title bar eating space), enlarge once */
            {
                static int enlarged = 0;
                if (!enlarged) {
                    int curW = 0, curH = 0;
                    FILE *f = fopen("/tmp/zt2_winsize", "r");
                    if (f) { fscanf(f, "%d %d", &curW, &curH); fclose(f); }
                    if (curW > 0 && curW <= 640 && curH < 480) {
                        /* Content is smaller than 640x480 - enlarge to compensate for title bar */
                        NSSize newContentSize = {640, 480};
                        ((void(*)(id,SEL,NSSize))objc_msgSend)(win,
                            sel_registerName("setContentSize:"), newContentSize);
                        enlarged = 1;
                    }
                }
            }
            unsigned long mask = ((unsigned long(*)(id,SEL))objc_msgSend)(win, sel_registerName("styleMask"));
            if (!(mask & 8))
                ((void(*)(id,SEL,unsigned long))objc_msgSend)(win, sel_registerName("setStyleMask:"), mask | 8);
            NSSize minSz = {640, 480}, maxSz = {10000, 10000};
            ((void(*)(id,SEL,NSSize))objc_msgSend)(win, sel_registerName("setContentMinSize:"), minSz);
            ((void(*)(id,SEL,NSSize))objc_msgSend)(win, sel_registerName("setContentMaxSize:"), maxSz);

            /* Resize subviews to expand GL framebuffer (flushBuffer does the scaling blit) */
            {
                int targetW = 0, targetH = 0;
                FILE *f = fopen("/tmp/zt2_winsize", "r");
                if (f) { fscanf(f, "%d %d", &targetW, &targetH); fclose(f); }
                if (targetW > 650 && targetH > 490) {
                    id contentView = ((id(*)(id,SEL))objc_msgSend)(win, sel_registerName("contentView"));
                    if (contentView) {
                        id subviews = ((id(*)(id,SEL))objc_msgSend)(contentView, sel_registerName("subviews"));
                        if (subviews) {
                            unsigned long svCount = ((unsigned long(*)(id,SEL))objc_msgSend)(subviews, sel_registerName("count"));
                            unsigned long si;
                            for (si = 0; si < svCount; si++) {
                                id sv = ((id(*)(id,SEL,unsigned long))objc_msgSend)(subviews, sel_registerName("objectAtIndex:"), si);
                                NSSize newSize = {(CGFloat)targetW, (CGFloat)targetH};
                                struct objc_super sup;
                                sup.receiver = sv;
                                sup.super_class = class_getSuperclass(object_getClass(sv));
                                ((void(*)(struct objc_super*, SEL, NSSize))objc_msgSendSuper)(&sup,
                                    sel_registerName("setFrameSize:"), newSize);
                            }
                            ((void(*)(id,SEL))objc_msgSend)(win, sel_registerName("updateForGLSubviews"));
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

__attribute__((constructor))
static void wine_aspect_init(void) {
    pthread_t t;
    pthread_create(&t, NULL, aspect_thread, NULL);
    pthread_detach(t);
}
