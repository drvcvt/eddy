#include "popovers/exportpanel.h"
#include "app/fills.h"
#include "video/mediaio.h"
#include "app/theme.h"
#include <QAbstractButton>
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace eddy {

static const int kSides[] = {0, 1080, 720, 480};
static const int kRates[] = {60, 30, 15};

ExportPanel::ExportPanel(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ExportPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(theme::kS3, theme::kS3, theme::kS3, theme::kS3);
    grid->setHorizontalSpacing(theme::kS3);
    grid->setVerticalSpacing(theme::kS2);
    auto *title = new QLabel(tr("Export"), this);
    title->setObjectName(QStringLiteral("ExportSection"));
    grid->addWidget(title, m_row++, 0, 1, 2);

    m_preset = addRow(tr("Preset"), {tr("Original"), tr("Web"), tr("Small"), tr("GIF"), tr("10 MB")});
    m_format = addRow(tr("Format"), {QStringLiteral("MP4"), QStringLiteral("WebM"), QStringLiteral("GIF")});
    m_size = addRow(tr("Size"), {tr("Full"), QStringLiteral("1080"), QStringLiteral("720"), QStringLiteral("480")});
    m_fps = addRow(tr("Frames"), {QStringLiteral("60"), QStringLiteral("30"), QStringLiteral("15")});
    m_sound = addRow(tr("Sound"), {tr("On"), tr("Off")});
    m_soundRow = m_videoOnly.mid(m_videoOnly.size() - 2);   // shown by setSound, for clips with sound
    m_videoOnly.resize(m_videoOnly.size() - 2);
    for (QWidget *w : std::as_const(m_soundRow)) w->hide();
    m_preset->button(0)->setToolTip(tr("The source's format and size, at most 60 frames"));
    m_preset->button(4)->setToolTip(tr("At most 10 MB, for chat and pull requests\nSize and frames still choose within it"));
    m_size->button(0)->setToolTip(tr("Full size"));
    for (int i = 1; i < 4; ++i) m_size->button(i)->setToolTip(tr("Shorter side at most %1 px").arg(kSides[i]));

    connect(m_preset, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings = exportPreset(ExportPreset(id));
        sync();
        emit settingsChanged(m_settings);
    });
    connect(m_format, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.format = ExportSettings::Format(id + 1);
        if (m_settings.format == ExportSettings::Format::Gif) m_settings.limitMb = 0;   // a GIF has no rate to cap
        sync();
        emit settingsChanged(m_settings);
    });
    connect(m_size, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.shortSide = kSides[id];
        sync();
        emit settingsChanged(m_settings);
    });
    connect(m_fps, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.fps = kRates[id];
        sync();
        emit settingsChanged(m_settings);
    });
    // The sound is the document's (undoable, shared with the speaker's menu), not a setting.
    connect(m_sound, &QButtonGroup::idClicked, this, [this](int id) { emit soundChosen(id == 0); });

    auto *footer = new QHBoxLayout;
    footer->setSpacing(theme::kS2);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("ExportSummary"));
    m_summary->setToolTip(tr("Output size and duration"));
    footer->addWidget(m_summary);
    footer->addStretch(1);
    m_save = new QToolButton(this);
    m_save->setObjectName(QStringLiteral("ExportSave"));
    m_save->setCursor(Qt::PointingHandCursor);
    m_save->setFixedHeight(theme::kCtl);
    connect(m_save, &QToolButton::clicked, this, &ExportPanel::saveRequested);
    footer->addWidget(m_save);
    grid->setRowMinimumHeight(m_row++, theme::kS1);
    grid->addLayout(footer, m_row++, 0, 1, 2);
    // Projects keep the original and every layer editable (21.09. plan 5).
    auto *projects = new QHBoxLayout;
    projects->setSpacing(theme::kS1);
    auto projectButton = [&](const QString &text, const QString &name, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setText(text);
        b->setToolTip(tip);
        b->setAccessibleName(text);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kCtl);
        projects->addWidget(b);
        return b;
    };
    connect(projectButton(tr("Save project…"), QStringLiteral("ProjectSave"),
                          tr("Keep the original and every layer editable\tCtrl+Shift+S")),
            &QToolButton::clicked, this, &ExportPanel::projectSaveRequested);
    connect(projectButton(tr("Open project…"), QStringLiteral("ProjectOpen"), tr("Open an Eddy project\tCtrl+O")),
            &QToolButton::clicked, this, &ExportPanel::projectOpenRequested);
    connect(projectButton(tr("Resume…"), QStringLiteral("ProjectResume"), tr("Edits kept since they were last open")),
            &QToolButton::clicked, this, &ExportPanel::resumeRequested);
    projects->addStretch(1);
    grid->addLayout(projects, m_row++, 0, 1, 2);
    sync();
}

QButtonGroup *ExportPanel::addRow(const QString &label, const QStringList &choices) {
    auto *grid = static_cast<QGridLayout *>(layout());
    auto *caption = new QLabel(label, this);
    caption->setObjectName(QStringLiteral("ExportLabel"));
    grid->addWidget(caption, m_row, 0);
    m_videoOnly << caption;
    // A groove on raise2 holding 20 px segments; the choice is a step brighter.
    auto *groove = new QWidget(this);
    groove->setObjectName(QStringLiteral("ExportSegments"));
    groove->setAttribute(Qt::WA_StyledBackground, true);
    auto *row = new QHBoxLayout(groove);
    row->setContentsMargins(theme::kS1, theme::kS1, theme::kS1, theme::kS1);   // kRChip segments in a kRBar groove
    row->setSpacing(0);
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < choices.size(); ++i) {
        auto *b = new QToolButton(groove);
        b->setObjectName(QStringLiteral("ExportSegment"));
        b->setText(choices[i]);
        b->setCheckable(true);
        b->setFixedHeight(theme::kCtl);
        b->setCursor(Qt::PointingHandCursor);
        b->setAccessibleName(label + QLatin1Char(' ') + choices[i]);
        group->addButton(b, i);
        row->addWidget(b);
    }
    new SelectionGlide(group, groove, theme::Tone::SegmentOn, SelectionGlide::Shape::Chip);
    grid->addWidget(groove, m_row++, 1, Qt::AlignLeft);
    m_videoOnly << groove;
    return group;
}

void ExportPanel::setSettings(const ExportSettings &settings) {
    m_settings = settings;
    sync();
}

void ExportPanel::setVideo(bool video) {
    m_video = video;
    for (QWidget *w : std::as_const(m_videoOnly)) w->setVisible(video);
    for (QWidget *w : std::as_const(m_soundRow)) w->setVisible(video && m_soundAvailable);
    sync();
    adjustSize();
}

void ExportPanel::setKnownSize(std::optional<qint64> bytes) {
    m_knownSize = bytes;
    sync();
}

void ExportPanel::setSound(bool on, bool available) {
    m_soundOn = on;
    const bool shown = available != m_soundAvailable;
    m_soundAvailable = available;
    if (shown) setVideo(m_video);
    else sync();
}

void ExportPanel::setOutput(QSize framed, qint64 durationMs, const QString &sourcePath) {
    m_framed = framed;
    m_durationMs = durationMs;
    m_sourcePath = sourcePath;
    sync();
}

void ExportPanel::sync() {
    // A changed row leaves every preset: no separate "Custom" chip.
    auto check = [](QButtonGroup *group, int id) {
        group->setExclusive(false);
        for (QAbstractButton *b : group->buttons()) b->setChecked(group->id(b) == id);
        group->setExclusive(id >= 0);
    };
    const auto preset = presetOf(m_settings);
    check(m_preset, preset ? int(*preset) : -1);
    // Original shows the source's own container where the row has it.
    const QString source = exportSuffix(ExportSettings{}, m_sourcePath);
    check(m_format, m_settings.format != ExportSettings::Format::Original ? int(m_settings.format) - 1
        : source == QLatin1String("mp4") ? 0 : source == QLatin1String("webm") ? 1 : -1);
    check(m_size, int(std::find(std::begin(kSides), std::end(kSides), m_settings.shortSide) - std::begin(kSides)));
    check(m_fps, int(std::find(std::begin(kRates), std::end(kRates), m_settings.fps) - std::begin(kRates)));
    // A GIF has no sound to carry.
    const bool gif = m_settings.format == ExportSettings::Format::Gif;
    check(m_sound, gif || !m_soundOn ? 1 : 0);
    for (QAbstractButton *b : m_sound->buttons()) {
        b->setEnabled(!gif);
        b->setToolTip(gif ? tr("A GIF has no sound") : QString());
    }
    const QSize out = exportSize(m_framed, m_settings.shortSide);
    // A gap, not a glyph, between size and time.
    // The size once it is known exactly; under a limit, the limit until then.
    const QString size = !m_video ? QString()
        : m_knownSize ? QStringLiteral("   %1\u00a0MB").arg(*m_knownSize / 1e6, 0, 'f', *m_knownSize < 10e6 ? 1 : 0)
        : m_settings.limitMb > 0 ? QStringLiteral("   max %1\u00a0MB").arg(m_settings.limitMb) : QString();
    m_summary->setText(m_framed.isEmpty() ? QString()
        : m_video ? QStringLiteral("%1 × %2   %3").arg(out.width()).arg(out.height()).arg(formatTime(m_durationMs)) + size
                  : QStringLiteral("%1 × %2").arg(m_framed.width()).arg(m_framed.height()));
    const QString suffix = m_video ? exportSuffix(m_settings, m_sourcePath) : QStringLiteral("png");
    m_save->setText(tr("Save %1").arg(suffix == QLatin1String("webm") ? QStringLiteral("WebM") : suffix.toUpper()));
    m_save->setAccessibleName(m_save->text());
}

}
