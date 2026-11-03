// A text written as by a pen: the order of the pen on shapes known in advance, the reveal, and the Cursive generator in the
// engine (its transport, what is saved, the pixels rendered). NYXMAP_TEST_SHOTS: a folder for pictures of it being written.
#include "Engine.h"
#include "TextWriting.h"

#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <cstdio>

namespace {
int s_failures = 0;
void check(bool ok, const char *what)
{
    if (ok) return;
    std::printf("FAIL text_writing: %s\n", what);
    ++s_failures;
}

QImage canvas(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    return img;
}

int shownPixels(const QImage &img)
{
    int count = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) count += img.constScanLine(y)[x * 4 + 3] > 0;
    return count;
}

void shapes()
{
    // A stroke: the pen goes from its left to its right
    {
        QImage ink = canvas(200, 40);
        { QPainter p(&ink); p.fillRect(10, 15, 180, 10, Qt::white); }
        auto w = TextWriting::build(ink, ink, false, 40);
        check(w->orderAt(20, 20) < w->orderAt(100, 20) && w->orderAt(100, 20) < w->orderAt(180, 20), "a stroke, from left to right");
        check(std::abs(w->length() - 170) < 40, "its way, about its length");
        // Nothing, then all of it, then back
        w->setProgress(0);
        check(shownPixels(w->image()) == 0, "at 0: nothing");
        w->setProgress(0.5);
        const int half = shownPixels(w->image());
        check(half > 500 && half < 1400, "halfway: about half of it");
        check(w->image().pixelColor(30, 20).alpha() == 255 && w->image().pixelColor(170, 20).alpha() == 0, "the left written, not the right");
        w->setProgress(1);
        check(w->image() == ink, "at 1: the picture as it is");
        w->setProgress(0.25);
        check(shownPixels(w->image()) < half, "back: it is written less");
        w->setProgress(0);
        check(shownPixels(w->image()) == 0, "back to nothing");
    }
    // Two pieces on a line, then a line under: the order of the reading, whatever their order in the picture
    {
        QImage ink = canvas(200, 120);
        {
            QPainter p(&ink);
            p.fillRect(120, 10, 60, 8, Qt::white);
            p.fillRect(20, 10, 60, 8, Qt::white);
            p.fillRect(20, 80, 60, 8, Qt::white);
        }
        auto w = TextWriting::build(ink, ink, false, 60);
        check(w->orderAt(70, 14) < w->orderAt(130, 14), "left piece before the right one");
        check(w->orderAt(170, 14) < w->orderAt(30, 84), "the first line before the second");
    }
    // A picture stored bottom row first: the same pixels
    {
        QImage ink = canvas(100, 40);
        { QPainter p(&ink); p.fillRect(10, 5, 80, 6, Qt::white); }
        auto w = TextWriting::build(ink, ink.mirrored(false, true), true, 40);
        check(w->orderAt(20, 8) >= 0 && w->orderAt(20, 35) < 0, "bottom up: the text at its place");
        w->setProgress(1);
        check(w->image().pixelColor(20, 40 - 1 - 8).alpha() == 255, "and revealed there");
    }
    // A loop, a crossing and a dot: every pixel gets its turn; an outline around the ink too
    {
        QImage ink = canvas(160, 100), full = canvas(160, 100);
        {
            QPainter p(&ink);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::white, 5));
            p.drawEllipse(20, 20, 50, 50);
            p.drawLine(10, 45, 130, 45);
            p.drawLine(100, 20, 100, 80);
            p.setBrush(Qt::white);
            p.drawEllipse(140, 10, 6, 6);
            QPainter f(&full);
            f.setRenderHint(QPainter::Antialiasing);
            f.setPen(QPen(Qt::red, 11));
            f.drawLine(10, 45, 130, 45);
            f.setPen(QPen(Qt::white, 5));
            f.drawEllipse(20, 20, 50, 50);
        }
        auto w = TextWriting::build(ink, full, false, 100);
        int missing = 0;
        for (int y = 0; y < full.height(); ++y)
            for (int x = 0; x < full.width(); ++x)
                missing += full.constScanLine(y)[x * 4 + 3] > 0 && w->orderAt(x, y) < 0;
        check(missing == 0, "every pixel has its turn");
        w->setProgress(1);
        check(w->image() == full, "the outline is written with its ink");
    }
    // Nothing at all
    {
        const QImage none = canvas(20, 20);
        auto w = TextWriting::build(none, none, false, 10);
        w->setProgress(0.5);
        check(w->length() == 0 && shownPixels(w->image()) == 0, "no text: nothing to write");
    }
}

void generator(Engine &e, const QString &shots)
{
    e.newProject();
    e.setFadesManual(true);
    e.setCompositionSize(QSize(960, 270));
    e.setViewportSize(e.indexOfId(e.mainViewportId()), QSize(960, 270));
    e.fadeCompositionOpacity(1, 0);
    const int index = e.addLayer(QStringLiteral("Cursive"));
    const quint64 id = e.layerId(index);
    check(e.setLayerCursive(index), "the generator");
    {
        Engine::Lock lk(&e.mutex());
        Layer *l = e.layer(index);
        check(l->text.cursive && l->text.font != TextSource().font, "in a cursive font");
        l->text.width = 960;
        l->text.height = 270;
        l->srcWidth = 960;
        l->srcHeight = 270;
    }
    e.setLayerTextContent(index, QStringLiteral("Bonjour écriture"));
    const auto rendered = [&](const char *name) {
        e.renderFrame();
        const QImage img = e.grabViewport(e.mainViewportId());
        if (!shots.isEmpty()) img.save(shots + QStringLiteral("/%1.png").arg(QLatin1String(name)));
        int lit = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) lit += qGray(img.pixel(x, y)) > 128;
        return lit;
    };
    const int whole = rendered("written-all");
    check(whole > 500, "written: all of the text");
    check(e.parameterValue(id, "text/write/progress").toDouble() == 1, "as any text until it is asked");

    e.setParameter(id, "text/write/duration", 2.0);
    e.setParameter(id, "text/write/start", true);
    check(rendered("written-0") == 0, "start: nothing yet");
    int before = 0;
    int k = 0;
    const char *names[] = {"written-1", "written-2", "written-3"};
    for (double dt : {0.5, 0.5, 0.5}) {
        e.advanceFades(dt);
        const int lit = rendered(names[k++]);
        check(lit > before && lit < whole, "more of it as the pen goes");
        before = lit;
    }
    check(std::abs(e.parameterValue(id, "text/write/progress").toDouble() - 0.75) < 0.01, "at its pace");
    e.setParameter(id, "text/write/pause", true);
    e.advanceFades(0.5);
    check(std::abs(e.parameterValue(id, "text/write/progress").toDouble() - 0.75) < 0.01, "paused: the pen holds");
    e.setParameter(id, "text/write/resume", true);
    e.advanceFades(1.0);
    check(e.parameterValue(id, "text/write/progress").toDouble() == 1, "goes on to its end");
    check(rendered("written-end") == whole, "all of it, as before");

    // Driven like any value, from a timeline or a snapshot's fade
    e.setParameter(id, "text/write/progress", 0.4);
    const int driven = rendered("written-driven");
    check(driven > 0 && driven < whole, "written as far as its value says");

    // Saved; a Text generator is not cursive
    QJsonObject saved = e.layerJson(index);
    check(saved.value("source").toObject().value("cursive").toBool(), "saved as cursive");
    check(e.setLayerText(index), "a plain text in its place");
    {
        Engine::Lock lk(&e.mutex());
        check(!e.layer(index)->text.cursive, "is not cursive");
    }
    e.setFadesManual(false);
    e.newProject();
}
} // namespace

int runTextWritingTests(Engine &e)
{
    s_failures = 0;
    shapes();
    generator(e, qEnvironmentVariable("NYXMAP_TEST_SHOTS"));
    std::printf("text_writing: %s (%d failure(s))\n", s_failures ? "FAILED" : "OK", s_failures);
    return s_failures;
}
