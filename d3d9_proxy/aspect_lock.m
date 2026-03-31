/*
 * aspect_lock.m - Force 4:3 via setContentAspectRatio
 * Uses pthread (proven to work in Wine) + NSWindow enumeration
 */
#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

static void *aspectThread(void *arg) {
    (void)arg;
    fprintf(stderr, "ASPECT: thread started\n");

    for (int i = 0; i < 30; i++) {
        sleep(2);

        @autoreleasepool {
            /* Try multiple ways to find windows */
            NSArray *appWindows = [NSApp windows];
            fprintf(stderr, "ASPECT: [NSApp windows]=%lu\n", (unsigned long)appWindows.count);

            /* Try window numbers */
            NSArray *nums = [NSWindow windowNumbersWithOptions:0];
            fprintf(stderr, "ASPECT: windowNumbers=%lu\n", (unsigned long)nums.count);

            /* Try to get windows via window number */
            for (NSNumber *num in nums) {
                NSWindow *win = [NSApp windowWithWindowNumber:[num integerValue]];
                if (win) {
                    fprintf(stderr, "ASPECT: found via number: %s %.0fx%.0f\n",
                            [win.title UTF8String], win.frame.size.width, win.frame.size.height);
                    if (win.frame.size.width > 200) {
                        [win setContentAspectRatio:NSMakeSize(4.0, 3.0)];
                        win.styleMask |= NSWindowStyleMaskResizable;
                        fprintf(stderr, "ASPECT: SET 4:3!\n");
                        return NULL; /* done */
                    }
                }
            }

            /* Also try iterating NSApp windows */
            for (NSWindow *win in appWindows) {
                fprintf(stderr, "ASPECT: app window: %s %.0fx%.0f\n",
                        [win.title UTF8String], win.frame.size.width, win.frame.size.height);
                if (win.frame.size.width > 200) {
                    [win setContentAspectRatio:NSMakeSize(4.0, 3.0)];
                    win.styleMask |= NSWindowStyleMaskResizable;
                    fprintf(stderr, "ASPECT: SET 4:3!\n");
                    return NULL;
                }
            }
        }
    }
    fprintf(stderr, "ASPECT: gave up after 60 seconds\n");
    return NULL;
}

__attribute__((constructor))
static void init(void) {
    fprintf(stderr, "ASPECT: loaded pid=%d\n", getpid());
    pthread_t t;
    pthread_create(&t, NULL, aspectThread, NULL);
    pthread_detach(t);
}
