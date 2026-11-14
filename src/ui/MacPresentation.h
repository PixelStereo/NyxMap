#pragma once
// macOS only.

class QWindow;

// Fullscreen output without a Space of its own, like the "simple fullscreen" of other VJ applications:
// the borderless window is raised above the menu bar and the Dock of its screen, so entering and
// leaving are instant (native fullscreen animates a new Space for about a second). Off restores the
// normal window level.
void macSetCoversScreen(QWindow *w, bool covers);

// For as long as NyxMap runs: no App Nap, no timer coalescing, no idle sleep of the machine (a live show)
void macKeepLiveOutputActive();
