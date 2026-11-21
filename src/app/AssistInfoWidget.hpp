#pragma once

#include <QWidget>
#include <QString>

class QResizeEvent;
class QPaintEvent;

namespace chroma::app {

/// ViewportWidget 左上角的信息浮层；半透明，不接收鼠标。
class AssistInfoWidget final : public QWidget {
    Q_OBJECT
public:
    explicit AssistInfoWidget(QWidget* parent = nullptr);
    void setInfo(const QString& text);
    const QString& info() const { return m_info; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_info;
};

} // namespace chroma::app
