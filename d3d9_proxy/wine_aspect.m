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

typedef NSSize (*WindowWillResizeIMP)(id, SEL, id, NSSize);
static WindowWillResizeIMP orig_windowWillResize = NULL;
static NSSize swz_windowWillResize(id self, SEL _cmd, id sender, NSSize frameSize) {
    id title = ((id(*)(id,SEL))objc_msgSend)(self, sel_registerName("title"));
    if (title) {
        const char *t = ((const char*(*)(id,SEL))objc_msgSend)(title, sel_registerName("UTF8String"));
        if (t && strstr(t, "Zoo Tycoon")) return frameSize;
    }
    return orig_windowWillResize(self, _cmd, sender, frameSize);
}

/* Block Wine's setFrameFromWine 640x480 resets when user has resized */
typedef void (*SetFrameFromWineIMP)(id, SEL, NSRect);
static SetFrameFromWineIMP orig_setFrameFromWine = NULL;
static void swz_setFrameFromWine(id self, SEL _cmd, NSRect contentRect) {
    if (contentRect.size.width < 650 && contentRect.size.height < 490) {
        /* During live resize or when window is larger, block the 640x480 reset */
        BOOL inResize = ((BOOL(*)(id,SEL))objc_msgSend)(self, sel_registerName("inLiveResize"));
        if (inResize) return;

        /* Also check size file */
        int curW = 0, curH = 0;
        FILE *f = fopen("/tmp/zt2_winsize", "r");
        if (f) { fscanf(f, "%d %d", &curW, &curH); fclose(f); }
        if (curW > 660 && curH > 500) {
            contentRect.size.width = curW;
            contentRect.size.height = curH;
        }
    }
    orig_setFrameFromWine(self, _cmd, contentRect);
}

static int g_swizzled = 0;

static void *aspect_thread(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < 600; i++) {
        usleep(200000);
        Class WineWindow = objc_getClass("WineWindow");
        if (!WineWindow) continue;
        if (!g_swizzled) {
            Method m;
            m = class_getInstanceMethod(WineWindow, sel_registerName("preventResizing"));
            if (m) method_setImplementation(m, (IMP)swz_preventResizing);
            m = class_getInstanceMethod(WineWindow, sel_registerName("adjustFeaturesForState"));
            if (m) { orig_adjustFeatures = (AdjustFeaturesIMP)method_getImplementation(m); method_setImplementation(m, (IMP)swz_adjustFeaturesForState); }
            m = class_getInstanceMethod(WineWindow, sel_registerName("windowWillResize:toSize:"));
            if (m) { orig_windowWillResize = (WindowWillResizeIMP)method_getImplementation(m); method_setImplementation(m, (IMP)swz_windowWillResize); }
            m = class_getInstanceMethod(WineWindow, sel_registerName("setFrameFromWine:"));
            if (m) { orig_setFrameFromWine = (SetFrameFromWineIMP)method_getImplementation(m); method_setImplementation(m, (IMP)swz_setFrameFromWine); }
            g_swizzled = 1;
        }
        /* Just set aspect ratio on all WineWindows */
        id app = ((id(*)(id,SEL))objc_msgSend)((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
        if (!app) continue;
        id windows = ((id(*)(id,SEL))objc_msgSend)(app, sel_registerName("windows"));
        if (!windows) continue;
        unsigned long count = ((unsigned long(*)(id,SEL))objc_msgSend)(windows, sel_registerName("count"));
        unsigned long j;
        for (j = 0; j < count; j++) {
            id win = ((id(*)(id,SEL,unsigned long))objc_msgSend)(windows, sel_registerName("objectAtIndex:"), j);
            if (!win || !((BOOL(*)(id,SEL,Class))objc_msgSend)(win, sel_registerName("isKindOfClass:"), WineWindow)) continue;
            NSSize ratio = {4.0, 3.0};
            ((void(*)(id,SEL,NSSize))objc_msgSend)(win, sel_registerName("setContentAspectRatio:"), ratio);
            unsigned long mask = ((unsigned long(*)(id,SEL))objc_msgSend)(win, sel_registerName("styleMask"));
            if (!(mask & 8)) ((void(*)(id,SEL,unsigned long))objc_msgSend)(win, sel_registerName("setStyleMask:"), mask | 8);
            NSSize minSz = {640, 480}, maxSz = {10000, 10000};
            ((void(*)(id,SEL,NSSize))objc_msgSend)(win, sel_registerName("setContentMinSize:"), minSz);
            ((void(*)(id,SEL,NSSize))objc_msgSend)(win, sel_registerName("setContentMaxSize:"), maxSz);
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
