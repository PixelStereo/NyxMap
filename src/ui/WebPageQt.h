#pragma once
// The browser of the Web Page generator (WebPage.h), with Qt WebEngine: given to the engine at startup. Built only
// when Qt WebEngine is found (PINOL_HAS_WEB); without it this does nothing and the generator is not offered.

// Before any project is opened; the application (a QApplication) is already made. True when web pages are available.
bool registerWebPages();
