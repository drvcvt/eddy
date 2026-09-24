#pragma once
#include <QWidget>
#include "exportsettings.h"
class QButtonGroup;
class QLabel;
class QToolButton;
namespace eddy {

// The export popover's content at the Save button (studio plan 6.9, Q7 B2):
// segmented rows for preset, format, size and frame rate, the output size and
// duration, and the one Save action. Every choice is emitted at once.
class ExportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ExportPanel(QWidget *parent = nullptr);
    void setSettings(const ExportSettings &settings);
    // Images have no format rows; the size, Save and the project actions stay.
    void setVideo(bool video);
    ExportSettings settings() const { return m_settings; }
    // The framed output before any shrinking, the output duration and the
    // source path for Original's container.
    void setOutput(QSize framed, qint64 durationMs, const QString &sourcePath);
signals:
    void settingsChanged(const ExportSettings &settings);
    void saveRequested();
    void projectSaveRequested();
    void projectOpenRequested();
    void resumeRequested();
private:
    QButtonGroup *addRow(const QString &label, const QStringList &choices);
    void sync();
    ExportSettings m_settings;
    QSize m_framed;
    qint64 m_durationMs = 0;
    QString m_sourcePath;
    QButtonGroup *m_preset = nullptr, *m_format = nullptr, *m_size = nullptr, *m_fps = nullptr;
    QLabel *m_summary = nullptr;
    QToolButton *m_save = nullptr;
    QList<QWidget *> m_videoOnly;
    bool m_video = true;
    int m_row = 0;
};

}
