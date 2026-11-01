#include "PreviewWorkspace.h"
#include "MappingView.h"
#include "Scene3DView.h"
#include "Engine.h"
#include "GlDraw.h"
#include "FixtureOverlay.h"
#include "Widgets.h"
#include <QComboBox>
#include <QGridLayout>
#include <QJsonArray>
#include <QMenu>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QPainter>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

// A layer, a viewport or the composition as rendered, with the bar every preview has: zoom (buttons, wheel or
// pinch, around the cursor), Fit, and the shared magnet / auto-select switches. Pan: middle button, Alt/⌥ + drag,
// or two fingers on a trackpad.
class PreviewPicture : public QOpenGLWidget
{
public:
    PreviewPicture(Engine *engine, int slot) : m_engine(engine), m_slot(slot)
    {
        setMinimumSize(100, 70);
        m_bar = new ZoomBar(this, QStringLiteral("Whole picture in view\n"
                                                 "Pan: middle button, Alt/⌥ + drag, or two fingers on a trackpad"));
        m_bar->setObjectName(QStringLiteral("previewZoomBar%1").arg(slot));
        connect(m_bar, &ZoomBar::zoomOutClicked, this, [this] { zoomAt(QPointF(width() / 2.0, height() / 2.0), 1 / 1.25); });
        connect(m_bar, &ZoomBar::zoomInClicked, this, [this] { zoomAt(QPointF(width() / 2.0, height() / 2.0), 1.25); });
        connect(m_bar, &ZoomBar::fitClicked, this, [this] { fit(); });
        // Each view chooses whether it shows the pixel mapping fixtures (the composition's or a viewport's picture)
        m_fixturesButton = m_bar->addButton(QStringLiteral("Fixtures"),
                                            QStringLiteral("Show the pixel mapping fixtures over this view"));
        m_fixturesButton->setObjectName(QStringLiteral("previewFixtures%1").arg(slot));
        m_fixturesButton->setCheckable(true);
        m_fixturesButton->setChecked(QSettings().value(settingsKey(), false).toBool());
        connect(m_fixturesButton, &QToolButton::toggled, this, [this](bool on) {
            QSettings().setValue(settingsKey(), on);
            update();
        });
        connect(engine, &Engine::fixturesChanged, this, qOverload<>(&QWidget::update));
    }
    // The source shown (0: the composition): fixtures are drawn over the composition and the viewports only
    void setSource(quint64 id)
    {
        m_source = id;
        m_fixturesButton->setVisible(showsComposition());
        update();
    }
    bool fixturesShown() const { return m_fixturesButton->isChecked() && showsComposition(); }
    ~PreviewPicture() override {
        if (context()) { makeCurrent(); m_draw.destroy(); doneCurrent(); }
    }
    void fit()
    {
        m_zoom = 1.0;
        m_pan = QPointF();
        m_bar->setZoom(m_zoom);
        update();
    }
    double zoom() const { return m_zoom; }
    QJsonObject viewState() const
    {
        return {{"zoom", m_zoom}, {"pan", QJsonArray{m_pan.x(), m_pan.y()}}, {"fixtures", m_fixturesButton->isChecked()}};
    }
    void setViewState(const QJsonObject &o)
    {
        if (o.contains("zoom")) m_zoom = std::clamp(o.value("zoom").toDouble(1.0), 0.25, 64.0);
        const QJsonArray pan = o.value("pan").toArray();
        if (pan.size() == 2) m_pan = QPointF(pan[0].toDouble(), pan[1].toDouble());
        if (o.contains("fixtures")) m_fixturesButton->setChecked(o.value("fixtures").toBool());
        m_bar->setZoom(m_zoom);
        update();
    }

protected:
    void initializeGL() override { m_draw.init(); }
    void paintGL() override {
        auto *f = context()->extraFunctions();
        const qreal dpr = devicePixelRatioF();
        f->glViewport(0, 0, int(width() * dpr), int(height() * dpr));
        f->glClearColor(0.07f, 0.07f, 0.08f, 1);
        f->glClear(GL_COLOR_BUFFER_BIT);
        Engine::Lock lock(&m_engine->mutex());
        m_engine->waitForFrame(f);
        QSize size;
        int buffer = 0;
        const GLuint texture = m_engine->previewTexture(m_slot, &size, &buffer);
        if (!texture || size.isEmpty()) return;
        m_picture = size;
        const QRectF r = viewRect();
        // GL counts rows from the bottom
        f->glViewport(int(std::lround(r.left() * dpr)), int(std::lround((height() - r.bottom()) * dpr)),
                      int(std::lround(r.width() * dpr)), int(std::lround(r.height() * dpr)));
        m_draw.drawTexture(texture);
        m_engine->finishPreviewRead(m_slot, buffer, f);
        if (fixturesShown()) paintFixtures();
    }
    // Lock held
    void paintFixtures()
    {
        const FixturePatch patch = m_engine->fixturePatch();
        if (patch.fixtures.empty()) return;
        const int index = m_source ? m_engine->indexOfId(m_source) : -1;
        const Layer *viewport = index >= 0 ? m_engine->layer(index) : nullptr;
        const QRectF r = viewRect();
        // A composition point in the picture: through the viewport's frame when the picture is a viewport's
        auto toWidget = [&](QPointF n) {
            const QPointF uv = viewport ? viewport->mapping.unmap(n, n) : n;
            return QPointF(r.left() + uv.x() * r.width(), r.top() + uv.y() * r.height());
        };
        QPainter p(this);
        fixtureview::paint(p, patch, m_engine->compositionSize(), toWidget, m_engine->fixtureLooks());
    }
    void wheelEvent(QWheelEvent *e) override
    {
        // As in the composition's view: trackpad scroll pans, a pinch or Ctrl/⌘ + scroll or the mouse wheel zooms
        const bool trackpad = e->phase() != Qt::NoScrollPhase && !e->pixelDelta().isNull();
        if (trackpad && !(e->modifiers() & Qt::ControlModifier)) {
            m_pan += QPointF(e->pixelDelta());
            update();
        } else {
            const double steps = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->pixelDelta().y() * 2.0;
            zoomAt(e->position(), std::pow(1.0015, steps));
        }
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier))) {
            m_panning = true;
            m_panLast = e->position();
            e->accept();
            return;
        }
        QOpenGLWidget::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_panning) return QOpenGLWidget::mouseMoveEvent(e);
        m_pan += e->position() - m_panLast;
        m_panLast = e->position();
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (!m_panning) return QOpenGLWidget::mouseReleaseEvent(e);
        m_panning = false;
    }
    void mouseDoubleClickEvent(QMouseEvent *) override { fit(); }

private:
    QRectF fitRect() const
    {
        QSizeF s = m_picture.isEmpty() ? QSizeF(size()) : QSizeF(m_picture);
        s.scale(QSizeF(size()), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    }
    QRectF viewRect() const
    {
        const QRectF fit = fitRect();
        QRectF r(0, 0, fit.width() * m_zoom, fit.height() * m_zoom);
        r.moveCenter(fit.center() + m_pan);
        return r;
    }
    void zoomAt(QPointF at, double factor)
    {
        const QRectF before = viewRect();
        const QPointF n((at.x() - before.left()) / std::max(1.0, before.width()),
                        (at.y() - before.top()) / std::max(1.0, before.height()));
        m_zoom = std::clamp(m_zoom * factor, 0.25, 64.0);
        const QRectF fit = fitRect();
        const double w = fit.width() * m_zoom, h = fit.height() * m_zoom;
        m_pan = QPointF(at.x() - n.x() * w + w / 2, at.y() - n.y() * h + h / 2) - fit.center();
        m_bar->setZoom(m_zoom);
        update();
    }

    bool showsComposition() const
    {
        if (!m_source) return true;
        Engine::Lock lock(&m_engine->mutex());
        const int index = m_engine->indexOfId(m_source);
        return index >= 0 && m_engine->isViewport(index);
    }
    QString settingsKey() const { return QStringLiteral("view/fixtures/preview%1").arg(m_slot); }

    Engine *m_engine;
    int m_slot;
    quint64 m_source = 0;
    QToolButton *m_fixturesButton = nullptr;
    GlDraw m_draw;
    ZoomBar *m_bar = nullptr;
    QSize m_picture;
    double m_zoom = 1.0;
    QPointF m_pan, m_panLast;
    bool m_panning = false;
};

PreviewWorkspace::PreviewWorkspace(Engine *engine, MappingView *mapping, QWidget *parent)
    : QWidget(parent), m_engine(engine), m_mapping(mapping)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *count = m_countBox = new QComboBox;
    count->setObjectName(QStringLiteral("previewLayout"));
    count->addItems({QStringLiteral("1 view"), QStringLiteral("2 views"), QStringLiteral("3 views"), QStringLiteral("4 views")});
    layout->addWidget(count);
    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_grid, 1);
    for (int slot = 0; slot < 4; ++slot) {
        auto &cell = m_cells[size_t(slot)];
        cell.widget = new QWidget(this);
        auto *column = new QVBoxLayout(cell.widget);
        column->setContentsMargins(0, 0, 0, 0);
        cell.source = new QToolButton;
        cell.source->setObjectName(QStringLiteral("previewSource%1").arg(slot));
        cell.source->setText(QStringLiteral("Composition"));
        cell.source->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(cell.source);
        cell.source->setMenu(menu);
        connect(menu, &QMenu::aboutToShow, this, [this, slot, menu] {
            menu->clear();
            auto *composition = menu->addAction(QStringLiteral("Composition"));
            connect(composition, &QAction::triggered, this, [this, slot] { selectSource(slot, 0, QStringLiteral("Composition")); });
            auto *layers = menu->addMenu(QStringLiteral("Layers"));
            auto *viewports = menu->addMenu(QStringLiteral("Viewports"));
            Engine::Lock lock(&m_engine->mutex());
            for (int i = 0; i < m_engine->layerCount(); ++i) {
                const auto *layer = m_engine->layer(i);
                if (!layer) continue;
                const quint64 id = layer->id;
                const QString label = layer->scene3d ? layer->name + QStringLiteral(" (3D view)") : layer->name;
                auto *action = (layer->isViewport ? viewports : layers)->addAction(label);
                connect(action, &QAction::triggered, this, [this, slot, id, label] { selectSource(slot, id, label); });
            }
        });
        column->addWidget(cell.source);
        cell.pages = new QStackedWidget;
        cell.picture = new PreviewPicture(engine, slot);
        cell.pages->addWidget(cell.picture);
        cell.scene3d = new Scene3DView(engine, slot);
        cell.pages->addWidget(cell.scene3d);
        connect(cell.scene3d, &Scene3DView::layerPicked, this, &PreviewWorkspace::layerPicked);
        if (slot == 0) cell.pages->addWidget(mapping);
        column->addWidget(cell.pages, 1);
    }
    connect(count, &QComboBox::currentIndexChanged, this, [this](int index) { setCount(index + 1); });
    selectSource(0, 0, QStringLiteral("Composition"));
    setCount(1);
}

PreviewWorkspace::~PreviewWorkspace()
{
    for (int slot = 0; slot < 4; ++slot) m_engine->setPreviewSource(slot, 0);
}

void PreviewWorkspace::selectSource(int slot, quint64 id, const QString &label)
{
    auto &cell = m_cells[size_t(slot)];
    cell.id = id;
    cell.source->setText(label);
    bool scene = false;
    if (id) {
        Engine::Lock lock(&m_engine->mutex());
        const Layer *l = m_engine->layer(m_engine->indexOfId(id));
        scene = l && l->scene3d;
    }
    cell.pages->setCurrentWidget(scene                 ? static_cast<QWidget *>(cell.scene3d)
                                 : slot == 0 && !id ? static_cast<QWidget *>(m_mapping)
                                                    : cell.picture);
    m_engine->setPreviewSource(slot, id);
    cell.picture->setSource(id);
    cell.scene3d->setScene(scene ? id : 0); // a 3D scene: seen from the view's own camera
}

void PreviewWorkspace::setCount(int count)
{
    m_count = std::clamp(count, 1, 4);
    for (int slot = 0; slot < 4; ++slot) {
        auto &cell = m_cells[size_t(slot)];
        m_grid->removeWidget(cell.widget);
        cell.widget->setVisible(slot < m_count);
        if (slot < m_count) m_grid->addWidget(cell.widget, slot / 2, slot % 2, 1, m_count == 3 && slot == 2 ? 2 : 1);
        m_engine->setPreviewSource(slot, slot < m_count ? cell.id : 0);
    }
}

void PreviewWorkspace::refreshFrames()
{
    for (int slot = 0; slot < m_count; ++slot) {
        m_cells[size_t(slot)].picture->update();
        if (m_cells[size_t(slot)].scene3d->scene()) m_cells[size_t(slot)].scene3d->update();
    }
}

void PreviewWorkspace::setUndoStack(QUndoStack *undo)
{
    for (Cell &c : m_cells) c.scene3d->setUndoStack(undo);
}

void PreviewWorkspace::setCurrentLayer(quint64 id)
{
    for (Cell &c : m_cells) c.scene3d->setSelected(id);
}

QJsonObject PreviewWorkspace::state() const
{
    QJsonArray cells;
    for (const Cell &c : m_cells)
        cells.append(QJsonObject{{"source", QString::number(c.id)}, {"view", c.picture->viewState()}, {"view3d", c.scene3d->viewState()}});
    return {{"count", m_count}, {"cells", cells}, {"mapping", m_mapping->viewState()}};
}

void PreviewWorkspace::setState(const QJsonObject &o)
{
    const QJsonArray cells = o.value("cells").toArray();
    for (int slot = 0; slot < int(m_cells.size()) && slot < cells.size(); ++slot) {
        const QJsonObject c = cells[slot].toObject();
        const quint64 id = c.value("source").toString().toULongLong();
        QString label = QStringLiteral("Composition");
        if (id) {
            Engine::Lock lock(&m_engine->mutex());
            const Layer *layer = m_engine->layer(m_engine->indexOfId(id));
            if (!layer) continue; // gone: the view keeps what it shows
            label = layer->name;
        }
        selectSource(slot, id, label);
        m_cells[size_t(slot)].picture->setViewState(c.value("view").toObject());
        m_cells[size_t(slot)].scene3d->setViewState(c.value("view3d").toObject());
    }
    if (o.contains("mapping")) m_mapping->setViewState(o.value("mapping").toObject());
    if (o.contains("count")) m_countBox->setCurrentIndex(std::clamp(o.value("count").toInt(1), 1, 4) - 1);
}
