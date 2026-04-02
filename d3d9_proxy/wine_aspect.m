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

            /* Read actual window content size from /tmp/zt2_winsize (written by play.sh monitor) */
            int targetW = 0, targetH = 0;
            FILE *f = fopen("/tmp/zt2_winsize", "r");
            if (f) { fscanf(f, "%d %d", &targetW, &targetH); fclose(f); }
            if (targetW < 650 || targetH < 490) continue;

            /* Resize subviews to match window content area */
            id contentView = ((id(*)(id,SEL))objc_msgSend)(win, sel_registerName("contentView"));
            if (!contentView) continue;
            id subviews = ((id(*)(id,SEL))objc_msgSend)(contentView, sel_registerName("subviews"));
            if (!subviews) continue;
            unsigned long svCount = ((unsigned long(*)(id,SEL))objc_msgSend)(subviews, sel_registerName("count"));
            unsigned long si;
            for (si = 0; si < svCount; si++) {
                id sv = ((id(*)(id,SEL,unsigned long))objc_msgSend)(subviews,
                    sel_registerName("objectAtIndex:"), si);
                /* Get subview width via bounds.size.width workaround (no stret needed):
                   Use NSView.frame.size which for the main content is 640x480 */
                NSSize svSize;
                /* We know Wine sets subviews to 640x480. Just check and resize if window is larger. */
                NSSize newSize = {(CGFloat)targetW, (CGFloat)targetH};
                struct objc_super sup;
                sup.receiver = sv;
                sup.super_class = class_getSuperclass(object_getClass(sv));
                ((void(*)(struct objc_super*, SEL, NSSize))objc_msgSendSuper)(&sup,
                    sel_registerName("setFrameSize:"), newSize);
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
