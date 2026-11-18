#pragma once
// The Controls page of the remote (RemotePage.h), shown inside the application: the same page a phone gets, in a web view. The
// browser (Qt WebEngine) is only in the application program (WebPageQt.cpp gives its factory at startup), and the page comes from a
// small server of its own on the loopback, with a code of its own: the Remote setting is for the other devices. Without a browser
// nothing is offered and the Controls panel stays as it was.
#include <QString>
#include <functional>

class QWidget;
class QUrl;

namespace controlsweb {
using Factory = QWidget *(*)(const QString &url);
void setFactory(Factory factory);          // the application's: a web view that shows the page at this address
void setUrlProvider(std::function<QString()> provider); // the address of the page now ("": the server is not up)
bool available();                          // a browser is there (the address may not be yet)
QString url();
QWidget *make(const QString &url);         // a web view (null: no browser)
} // namespace controlsweb
