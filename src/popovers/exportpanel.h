#pragma once
#include <QWidget>
#include <optional>
#include "output/exportsettings.h"
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
    // Whether the output carries the clip's sound; the row shows only for clips that have one.
    void setSound(bool on, bool available);
    // The exact size of the file a delivery would hand out, once it is exported; C7
    // measured estimates before that off by up to 2x on screen recordings, so none show.
    void setKnownSize(std::optional<qint64> bytes);
signals:
    void settingsChanged(const ExportSettings &settings);
    void soundChosen(bool on);
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
    QButtonGroup *m_sound = nullptr;
    QList<QWidget *> m_soundRow;
    bool m_soundOn = true, m_soundAvailable = false;
    std::optional<qint64> m_knownSize;
    QLabel *m_summary = nullptr;
    QToolButton *m_save = nullptr;
    QList<QWidget *> m_videoOnly;
    bool m_video = true;
    int m_row = 0;
};

}
