#include "exportpanel.h"
#include "mediaio.h"
#include "theme.h"
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
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    auto *title = new QLabel(tr("EXPORT"), this);
    title->setObjectName(QStringLiteral("ExportSection"));
    QFont font = title->font();
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    title->setFont(font);
    grid->addWidget(title, m_row++, 0, 1, 2);

    m_preset = addRow(tr("Preset"), {tr("Original"), tr("Web"), tr("Small"), tr("GIF")});
    m_format = addRow(tr("Format"), {QStringLiteral("MP4"), QStringLiteral("WebM"), QStringLiteral("GIF")});
    m_size = addRow(tr("Size"), {tr("Full"), QStringLiteral("1080"), QStringLiteral("720"), QStringLiteral("480")});
    m_fps = addRow(tr("Frames"), {QStringLiteral("60"), QStringLiteral("30"), QStringLiteral("15")});
    m_preset->button(0)->setToolTip(tr("The source's format and size, at most 60 frames"));
    m_size->button(0)->setToolTip(tr("Full size"));
    for (int i = 1; i < 4; ++i) m_size->button(i)->setToolTip(tr("Shorter side at most %1 px").arg(kSides[i]));

    connect(m_preset, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings = exportPreset(ExportPreset(id));
        sync();
        emit settingsChanged(m_settings);
    });
    connect(m_format, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.format = ExportSettings::Format(id + 1);
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

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("ExportSummary"));
    m_summary->setToolTip(tr("Output size and duration"));
    footer->addWidget(m_summary);
    footer->addStretch(1);
    m_save = new QToolButton(this);
    m_save->setObjectName(QStringLiteral("ExportSave"));
    m_save->setCursor(Qt::PointingHandCursor);
    m_save->setFixedHeight(theme::kFloatButton.height());
    connect(m_save, &QToolButton::clicked, this, &ExportPanel::saveRequested);
    footer->addWidget(m_save);
    grid->setRowMinimumHeight(m_row++, 2);
    grid->addLayout(footer, m_row++, 0, 1, 2);
    // Projects keep the original and every layer editable (21.09. plan 5).
    auto *projects = new QHBoxLayout;
    projects->setSpacing(4);
    auto projectButton = [&](const QString &text, const QString &name, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setText(text);
        b->setToolTip(tip);
        b->setAccessibleName(text);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(theme::kFloatButton.height());
        projects->addWidget(b);
        return b;
    };
    connect(projectButton(tr("Save project…"), QStringLiteral("ProjectSave"),
                          tr("Keep the original and every layer editable\tCtrl+Shift+S")),
            &QToolButton::clicked, this, &ExportPanel::projectSaveRequested);
    connect(projectButton(tr("Open project…"), QStringLiteral("ProjectOpen"), tr("Open an Eddy project\tCtrl+O")),
            &QToolButton::clicked, this, &ExportPanel::projectOpenRequested);
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
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(0);
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < choices.size(); ++i) {
        auto *b = new QToolButton(groove);
        b->setObjectName(QStringLiteral("ExportSegment"));
        b->setText(choices[i]);
        b->setCheckable(true);
        b->setFixedHeight(20);
        b->setCursor(Qt::PointingHandCursor);
        b->setAccessibleName(label + QLatin1Char(' ') + choices[i]);
        group->addButton(b, i);
        row->addWidget(b);
    }
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
    sync();
    adjustSize();
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
    const QSize out = exportSize(m_framed, m_settings.shortSide);
    // A gap, not a glyph, between size and time.
    m_summary->setText(m_framed.isEmpty() ? QString()
        : m_video ? QStringLiteral("%1 × %2   %3").arg(out.width()).arg(out.height()).arg(formatTime(m_durationMs))
                  : QStringLiteral("%1 × %2").arg(m_framed.width()).arg(m_framed.height()));
    const QString suffix = m_video ? exportSuffix(m_settings, m_sourcePath) : QStringLiteral("png");
    m_save->setText(tr("Save %1").arg(suffix == QLatin1String("webm") ? QStringLiteral("WebM") : suffix.toUpper()));
    m_save->setAccessibleName(m_save->text());
}

}
