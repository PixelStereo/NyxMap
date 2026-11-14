#include "MacPresentation.h"

#import <AppKit/AppKit.h>

#include <QWindow>

static NSWindow *nsWindowOf(QWindow *w)
{
    if (!w || !w->handle()) return nil;
    NSView *view = reinterpret_cast<NSView *>(w->winId());
    return view ? [view window] : nil;
}

void macSetCoversScreen(QWindow *w, bool covers)
{
    NSWindow *win = nsWindowOf(w);
    if (!win) return;
    // Above the menu bar (and so the Dock) on any screen, whether or not NyxMap is the active application.
    [win setLevel:covers ? NSMainMenuWindowLevel + 1 : NSNormalWindowLevel];
    NSWindowCollectionBehavior b = [win collectionBehavior];
    b &= ~NSWindowCollectionBehaviorFullScreenPrimary; // never a Space of its own
    if (covers) b |= NSWindowCollectionBehaviorFullScreenNone;
    else b &= ~NSWindowCollectionBehaviorFullScreenNone;
    [win setCollectionBehavior:b];
}

void macKeepLiveOutputActive()
{
    // App Nap and timer coalescing slow the timers of an application macOS thinks is idle or in the background (the
    // render's pace, the cue clock). For live output: neither, and no idle sleep of the machine while NyxMap runs.
    static id<NSObject> activity = [[NSProcessInfo processInfo]
        beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                          reason:@"NyxMap: live video output and cues"];
    (void)activity;
}
