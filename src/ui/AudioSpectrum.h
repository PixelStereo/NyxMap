#pragma once
#include "AudioAnalysis.h"
#include <QWidget>
#include <functional>
class AudioSpectrum : public QWidget {
    Q_OBJECT
public:
    // source: the spectrum shown (the program mix's, a live input's), read 25 times a second while visible
    explicit AudioSpectrum(std::function<AudioAnalysis::Spectrum()> source, QWidget *parent=nullptr);
    void setBand(double low,double high);
    QSize minimumSizeHint() const override {return {220,135};}
signals:
    void bandEdited(double low,double high);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QRectF plot() const;
    double frequency(double x) const;
    double position(double hz) const;
    int partAt(double x) const; // 1: the low boundary, 2: the high one (within a few pixels), 3: the band between, 0: outside
    void drag(double x);
    std::function<AudioAnalysis::Spectrum()> m_source;
    AudioAnalysis::Spectrum m_spectrum;
    double m_low=20,m_high=20000;
    int m_handle=0;                              // what is dragged: a boundary (1, 2), the whole band (3)
    double m_grabX=0,m_grabLow=20,m_grabHigh=20000; // the band as it was where it was grabbed
};
