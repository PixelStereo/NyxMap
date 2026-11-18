#include "ControlsWeb.h"

namespace controlsweb {
namespace {
Factory s_factory = nullptr;
std::function<QString()> s_url;
} // namespace

void setFactory(Factory factory) { s_factory = factory; }
void setUrlProvider(std::function<QString()> provider) { s_url = std::move(provider); }
bool available() { return s_factory != nullptr; }
QString url() { return s_url ? s_url() : QString(); }
QWidget *make(const QString &url) { return s_factory && !url.isEmpty() ? s_factory(url) : nullptr; }
} // namespace controlsweb
