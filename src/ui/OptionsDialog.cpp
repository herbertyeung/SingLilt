// Application and current-score options with validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "OptionsDialog.h"
#include "InstrumentNames.h"
#include "audio/MicrophoneCapture.h"
#include "i18n/LanguageManager.h"
#include "settings/CapabilityStatus.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
QLabel *caption(const char *key)
{
    auto *label = new QLabel(trText(key));
    label->setProperty("_ui_text", QByteArray(key));
    label->setWordWrap(true);
    return label;
}
QSpinBox *integer(QFormLayout *form, const char *key, const char *name, int value, int low, int high)
{
    auto *box = new QSpinBox;
    box->setObjectName(name);
    box->setRange(low, high);
    box->setValue(value);
    form->addRow(caption(key), box);
    return box;
}
QDoubleSpinBox *number(QFormLayout *form, const char *key, const char *name, double value, double low, double high)
{
    auto *box = new QDoubleSpinBox;
    box->setObjectName(name);
    box->setRange(low, high);
    box->setValue(value);
    box->setSingleStep(0.1);
    box->setProperty("initialDisplayedValue", box->value());
    form->addRow(caption(key), box);
    return box;
}
double selectedNumber(QDoubleSpinBox *box, double original)
{
    // Keep unedited score values exact when the display rounds decimal places.
    return box->value() == box->property("initialDisplayedValue").toDouble() ? original : box->value();
}
QCheckBox *toggle(QFormLayout *form, const char *key, const char *name, bool checked)
{
    auto *box = new QCheckBox(trText(key));
    box->setObjectName(name);
    box->setProperty("_ui_text", QByteArray(key));
    box->setChecked(checked);
    form->addRow(box);
    return box;
}
QLineEdit *entry(QFormLayout *form, const char *key, const char *name, const QString &value)
{
    auto *edit = new QLineEdit(value);
    edit->setObjectName(name);
    form->addRow(caption(key), edit);
    return edit;
}
QComboBox *choices(QFormLayout *form, const char *key, const char *name, const QStringList &texts, int index)
{
    auto *box = new QComboBox;
    box->setObjectName(name);
    box->addItems(texts);
    box->setCurrentIndex(index);
    form->addRow(caption(key), box);
    return box;
}
QComboBox *program(QFormLayout *form, const char *key, const char *name, int value)
{
    auto *box = new QComboBox;
    box->setObjectName(name);
    for (int pc = 0; pc < 128; ++pc)
    {
        const char *instrumentKey = instrumentNameKey(pc);
        const char *translationKey = instrumentKey ? instrumentKey : "ui.instrument.gm";
        box->addItem(instrumentKey ? trText(translationKey) : trText(translationKey).arg(pc + 1), pc);
        box->setItemData(pc, QByteArray(translationKey), Qt::UserRole + 1);
        box->setItemData(pc, instrumentKey ? QStringList{} : QStringList{QString::number(pc + 1)},
                         Qt::UserRole + 2);
    }
    box->setCurrentIndex(value);
    form->addRow(caption(key), box);
    return box;
}
QFormLayout *page(QTabWidget *tabs)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *form = new QFormLayout(body);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setSpacing(12);
    scroll->setWidget(body);
    tabs->addTab(scroll, QString());
    return form;
}
bool containsField(QLayoutItem *item, QWidget *field)
{
    if (!item || !field)
        return false;
    if (item->widget())
        return item->widget() == field || item->widget()->isAncestorOf(field);
    if (auto *layout = item->layout())
        for (int i = 0; i < layout->count(); ++i)
            if (containsField(layout->itemAt(i), field))
                return true;
    return false;
}
QLabel *capabilityRow(QFormLayout *form, const char *label, const char *name)
{
    auto *status = new QLabel;
    status->setObjectName(name);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    form->addRow(caption(label), status);
    return status;
}
} // namespace

QLineEdit *OptionsDialog::pathRow(QFormLayout *form, const char *label, const char *name, const QString &path,
                                  bool directory)
{
    auto *row = new QHBoxLayout;
    auto *edit = new QLineEdit(path);
    edit->setObjectName(name);
    row->addWidget(edit);
    auto *choose = new QPushButton(trText("ui.options.browse"));
    choose->setProperty("_ui_text", QByteArray("ui.options.browse"));
    row->addWidget(choose);
    connect(choose, &QPushButton::clicked, this,
            [this, edit, directory]
            {
                const QString selected =
                    directory ? QFileDialog::getExistingDirectory(this, trText("ui.options.browse"), edit->text())
                              : QFileDialog::getOpenFileName(this, trText("ui.options.browse"), edit->text());
                if (!selected.isEmpty())
                    edit->setText(selected);
            });
    form->addRow(caption(label), row);
    return edit;
}

OptionsDialog::OptionsDialog(const AppSettings &s, const OptionsContext &c, QWidget *parent)
    : QDialog(parent), initial_(s), context_(c)
{
    setObjectName("applicationOptions");
    resize(860, 700);
    setMinimumSize(690, 510);
    auto *layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget;
    tabs_->setObjectName("optionsPages");
    layout->addWidget(tabs_, 1);
    auto *general = page(tabs_);
    const LanguageManager catalogs;
    const auto locales = catalogs.availableLanguages();
    QStringList names;
    for (const auto &locale : locales)
        names.append(catalogs.languageName(locale));
    language_ = choices(general, "ui.language.label", "optionsLanguage", names, locales.indexOf(s.language));
    for (int index = 0; index < locales.size(); ++index)
        language_->setItemData(index, locales[index]);
    theme_ = new QComboBox;
    theme_->setObjectName("optionsTheme");
    for (const auto &choice : {std::pair{ThemeMode::System, "ui.options.theme.system"},
                               std::pair{ThemeMode::Light, "ui.options.theme.light"},
                               std::pair{ThemeMode::Dark, "ui.options.theme.dark"}})
    {
        theme_->addItem(trText(choice.second), static_cast<int>(choice.first));
        theme_->setItemData(theme_->count() - 1, QByteArray(choice.second), Qt::UserRole + 1);
    }
    theme_->setCurrentIndex(theme_->findData(static_cast<int>(s.themeMode)));
    general->addRow(caption("ui.options.theme"), theme_);
    startup_ = toggle(general, "ui.options.startup_classroom", "optionsStartupClassroom", s.startupClassroom);
    markers_ = toggle(general, "ui.option.flags", "optionsMarkers", s.showMarkers);
    directory_ =
        pathRow(general, "ui.options.course_directory", "optionsCourseDirectory", s.lessonDirectory, true);
    general->addRow(caption("ui.options.defaults_help"));
    auto *audio = page(tabs_);
    backend_ = choices(audio, "ui.transport.source", "optionsAudioBackend",
                       {trText("ui.transport.source_piano"), trText("ui.transport.source_midi")}, s.audioBackend);
    gm_ = pathRow(audio, "ui.accompaniment.gm_label", "optionsGmPath", s.gmSoundFontPath, false);
    audio->addRow(caption("ui.options.gm_help"));
    pianoStatus_ = capabilityRow(audio, "ui.capabilities.piano", "optionsPianoStatus");
    gmStatus_ = capabilityRow(audio, "ui.capabilities.gm", "optionsGmStatus");
    outputBoost_ = toggle(audio, "ui.options.output_boost", "optionsOutputBoost", s.outputBoost);
    outputBoost_->setToolTip(trText("ui.options.output_boost_help"));
    outputBoost_->setEnabled(s.audioBackend == 0);
    connect(backend_, &QComboBox::currentIndexChanged, this,
            [this](int index) { outputBoost_->setEnabled(index == 0); });
    melodyVolume_ = number(audio, "ui.accompaniment.melody_volume", "optionsMelodyVolume", s.melodyVolume, 0, 1);
    accompanimentVolume_ =
        number(audio, "ui.accompaniment.volume", "optionsAccompanimentVolume", s.accompanimentVolume, 0, 1);
    originalVolume_ =
        number(audio, "ui.audio_import.original_volume", "optionsOriginalVolume", s.originalVolume, 0, 1);
    originalSpeed_ =
        number(audio, "ui.audio_import.original_speed", "optionsOriginalSpeed", s.originalSpeed, 0.25, 2);
    velocity_ = integer(audio, "ui.option.velocity", "optionsVelocity", s.velocity, 1, 127);
    accent_ = toggle(audio, "ui.option.accent", "optionsAccent", s.accentBeats);
    metronome_ = toggle(audio, "ui.transport.metronome", "optionsMetronome", s.metronome);
    programA_ = program(audio, "ui.transport.program_a", "optionsProgramA", s.programA);
    programB_ = program(audio, "ui.transport.program_b", "optionsProgramB", s.programB);
    auto *mic = page(tabs_);
    devices_ = new QComboBox;
    devices_->setObjectName("optionsMicrophone");
    devices_->addItem(trText("ui.options.default_device"), "");
    MicrophoneCapture microphone;
    for (const auto &device : microphone.devices())
        devices_->addItem(device.name, device.id);
    if (!s.microphoneId.isEmpty() && devices_->findData(s.microphoneId) < 0)
        devices_->addItem(trText("ui.options.missing_device"), s.microphoneId);
    devices_->setCurrentIndex(devices_->findData(s.microphoneId));
    mic->addRow(caption("ui.classroom.microphone"), devices_);
    tolerance_ = number(mic, "ui.classroom.tolerance", "optionsTolerance", s.centsTolerance, 10, 100);
    latency_ = integer(mic, "ui.classroom.latency", "optionsLatency", s.latencyMilliseconds, 0, 500);
    practiceSpeed_ = number(mic, "ui.classroom.speed", "optionsPracticeSpeed", s.practiceSpeed, 0.5, 1.5);
    difficulty_ = choices(mic, "ui.options.difficulty", "optionsDifficulty",
                          {trText("ui.ear.easy"), trText("ui.ear.medium"), trText("ui.ear.hard")}, s.difficulty);
    guide_ = choices(
        mic, "ui.options.guide", "optionsGuide",
        {trText("ui.classroom.independent"), trText("ui.classroom.follow"), trText("ui.classroom.accompaniment")},
        s.guide);
    auto *ai = page(tabs_);
    endpoint_ = entry(ai, "ui.dialog.endpoint", "optionsVisionEndpoint", s.vision.endpoint);
    model_ = entry(ai, "ui.dialog.model", "optionsVisionModel", s.vision.model);
    key_ = entry(ai, "ui.dialog.api_key", "optionsVisionKey", s.vision.apiKey);
    key_->setEchoMode(QLineEdit::Password);
    timeout_ = integer(ai, "ui.dialog.vision_timeout", "visionTimeout", s.vision.timeoutSeconds,
                       VisionConfig::MinTimeoutSeconds, VisionConfig::MaxTimeoutSeconds);
    ai->addRow(caption("ui.options.ai_help"));
    whisper_ = pathRow(ai, "ui.audio_import.model", "optionsWhisperModel", s.whisperModel, false);
    whisperStatus_ = capabilityRow(ai, "ui.capabilities.whisper", "optionsWhisperStatus");
    lyricLanguage_ = choices(ai, "ui.audio_import.lyric_language", "optionsLyricLanguage",
                             {trText("ui.audio_import.automatic"), "中文", "English"},
                             s.lyricLanguage == "zh"   ? 1
                             : s.lyricLanguage == "en" ? 2
                                                       : 0);
    separate_ = toggle(ai, "ui.vocal_separation.separate", "optionsSeparateVocals", s.separateVocals);
    recognize_ = toggle(ai, "ui.audio_import.recognize_lyrics", "optionsRecognizeLyrics", s.recognizeLyrics);
    enhance_ =
        toggle(ai, "ui.vocal_separation.enhance", "optionsEnhanceVoice", s.enhanceVoice && s.separateVocals);
    enhance_->setEnabled(s.separateVocals);
    connect(separate_, &QCheckBox::toggled, this,
            [this](bool separate)
            {
                enhance_->setEnabled(separate);
                if (!separate)
                    enhance_->setChecked(false);
            });
    separationStatus_ = capabilityRow(ai, "ui.capabilities.separation", "optionsSeparationStatus");
    ai->addRow(caption("ui.capabilities.local_check_help"));
    connect(gm_, &QLineEdit::textChanged, this, [this] { refreshCapabilities(); });
    connect(whisper_, &QLineEdit::textChanged, this, [this] { refreshCapabilities(); });
    auto *current = page(tabs_);
    auto *scope = caption(c.classroom ? "ui.options.practice_scope" : "ui.options.score_scope");
    current->addRow(scope);
    if (c.fullStaffPerformance)
        current->addRow(caption("ui.staff.preserved_performance"));
    QStringList keys;
    for (int i = 0; i < 12; ++i)
        keys.append(trText(qPrintable(QString("ui.key.%1").arg(i))));
    tonic_ = choices(current, "ui.option.key", "optionsCurrentKey", keys, c.score.tonic);
    meterTop_ =
        integer(current, "ui.option.meter", "optionsCurrentMeterTop", c.score.beatsPerBar, 1, MaximumBeatsPerBar);
    meterBottom_ = integer(current, "ui.options.meter_unit", "optionsCurrentMeterBottom", c.score.beatUnit, 1,
                           MaximumBeatUnit);
    tempo_ =
        number(current, "ui.option.tempo", "optionsCurrentTempo", c.score.bpm, MinimumScoreBpm, MaximumScoreBpm);
    if (c.fullStaffPerformance)
    {
        tonic_->setEnabled(false);
        meterTop_->setEnabled(false);
        meterBottom_->setEnabled(false);
    }
    transpose_ = integer(current, "ui.option.transpose", "optionsCurrentTranspose", c.transpose,
                         c.classroom ? -12 : -24, c.classroom ? 12 : 24);
    speed_ = number(current, "ui.classroom.speed", "optionsCurrentSpeed", c.speed, c.classroom ? 0.5 : 0.25,
                    c.classroom ? 1.5 : 2);
    currentVelocity_ =
        integer(current, "ui.option.velocity", "optionsCurrentVelocity", c.score.baseVelocity, 1, 127);
    currentAccent_ = toggle(current, "ui.option.accent", "optionsCurrentAccent", c.score.accentBeats);
    currentProgramA_ = program(
        current, c.fullStaffPerformance ? "ui.staff.primary_instrument" : "ui.transport.program_a",
        "optionsCurrentProgramA", c.fullStaffPerformance ? c.primaryStaffProgram : programForVerse(c.score, 0));
    currentProgramB_ = program(
        current, c.fullStaffPerformance ? "ui.staff.other_instrument" : "ui.transport.program_b",
        "optionsCurrentProgramB", c.fullStaffPerformance ? c.otherStaffProgram : programForVerse(c.score, 1));
    if (c.fullStaffPerformance)
        for (QWidget *field : std::initializer_list<QWidget *>{currentVelocity_, currentAccent_})
            field->setEnabled(false);
    melody_ =
        toggle(current, c.fullStaffPerformance ? "ui.staff.primary_parts" : "ui.accompaniment.melody_enabled",
               "optionsCurrentMelody", c.mix.melodyEnabled);
    accompaniment_ =
        toggle(current, c.fullStaffPerformance ? "ui.staff.other_parts" : "ui.accompaniment.accompaniment_enabled",
               "optionsCurrentAccompaniment", c.mix.accompanimentEnabled);
    currentMelodyVolume_ =
        number(current, c.fullStaffPerformance ? "ui.staff.primary_parts" : "ui.accompaniment.melody_volume",
               "optionsCurrentMelodyVolume", c.mix.melodyVolume, 0, 1);
    currentAccompanimentVolume_ =
        number(current, c.fullStaffPerformance ? "ui.staff.other_parts" : "ui.accompaniment.volume",
               "optionsCurrentAccompanimentVolume", c.mix.accompanimentVolume, 0, 1);
    source_ = choices(current, "ui.options.playback_source", "optionsCurrentSource",
                      {trText(c.fullStaffPerformance ? "ui.staff.piano_source" : "ui.audio_import.synthesized"),
                       trText("ui.audio_import.original"), trText("ui.vocal_separation.vocals_source"),
                       trText("ui.vocal_separation.instrumental_source")},
                      c.playbackSource);
    pattern_ = choices(
        current, "ui.options.pattern", "optionsCurrentPattern",
        {trText("ui.accompaniment.block"), trText("ui.accompaniment.arpeggio"), trText("ui.whole.sparse_pattern")},
        c.accompanimentPattern);
    pattern_->setEnabled(!c.fullStaffPerformance);
    currentOriginalSpeed_ =
        number(current, "ui.audio_import.original_speed", "optionsCurrentOriginalSpeed", c.originalSpeed, 0.25, 2);
    currentOriginalVolume_ =
        number(current, "ui.audio_import.original_volume", "optionsCurrentOriginalVolume", c.originalVolume, 0, 1);
    if (c.classroom)
    {
        // A course's notes and teaching metadata belong to its external file.
        for (QWidget *widget : std::initializer_list<QWidget *>{
                 tonic_, meterTop_, meterBottom_, tempo_, currentVelocity_, currentAccent_, currentProgramA_,
                 currentProgramB_, melody_, accompaniment_, currentMelodyVolume_, currentAccompanimentVolume_,
                 source_, pattern_, currentOriginalSpeed_, currentOriginalVolume_})
            widget->setEnabled(false);
    }
    error_ = new QLabel(this);
    error_->setObjectName("optionsValidationError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    error_->hide();
    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttons->setObjectName("optionsButtons");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this]
            {
                if (commit())
                    accept();
            });
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { commit(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    retranslate();
}

bool OptionsDialog::commit()
{
    auto s = initial_;
    auto c = context_;
    s.language = language_->currentData().toString();
    s.themeMode = static_cast<ThemeMode>(theme_->currentData().toInt());
    s.startupClassroom = startup_->isChecked();
    s.showMarkers = markers_->isChecked();
    s.lessonDirectory = directory_->text().trimmed();
    s.audioBackend = backend_->currentIndex();
    s.outputBoost = outputBoost_->isChecked();
    s.gmSoundFontPath = gm_->text().trimmed();
    s.melodyVolume = selectedNumber(melodyVolume_, s.melodyVolume);
    s.accompanimentVolume = selectedNumber(accompanimentVolume_, s.accompanimentVolume);
    s.originalVolume = selectedNumber(originalVolume_, s.originalVolume);
    s.originalSpeed = selectedNumber(originalSpeed_, s.originalSpeed);
    s.velocity = velocity_->value();
    s.accentBeats = accent_->isChecked();
    s.metronome = metronome_->isChecked();
    s.programA = programA_->currentData().toInt();
    s.programB = programB_->currentData().toInt();
    s.microphoneId = devices_->currentData().toString();
    s.centsTolerance = selectedNumber(tolerance_, s.centsTolerance);
    s.latencyMilliseconds = latency_->value();
    s.practiceSpeed = selectedNumber(practiceSpeed_, s.practiceSpeed);
    s.difficulty = difficulty_->currentIndex();
    s.guide = guide_->currentIndex();
    s.vision = {endpoint_->text().trimmed(), model_->text().trimmed(), key_->text(), timeout_->value()};
    s.whisperModel = whisper_->text().trimmed();
    s.lyricLanguage = lyricLanguage_->currentIndex() == 1   ? "zh"
                      : lyricLanguage_->currentIndex() == 2 ? "en"
                                                            : "auto";
    s.separateVocals = separate_->isChecked();
    s.recognizeLyrics = recognize_->isChecked();
    s.enhanceVoice = s.separateVocals && enhance_->isChecked();
    c.transpose = transpose_->value();
    c.speed = selectedNumber(speed_, c.speed);
    if (!c.classroom)
    {
        c.score.tonic = tonic_->currentIndex();
        c.score.beatsPerBar = meterTop_->value();
        c.score.beatUnit = meterBottom_->value();
        c.score.bpm = selectedNumber(tempo_, c.score.bpm);
        if (!c.fullStaffPerformance)
        {
            c.score.baseVelocity = currentVelocity_->value();
            c.score.accentBeats = currentAccent_->isChecked();
            if (programForVerse(c.score, 0) != currentProgramA_->currentData().toInt() ||
                programForVerse(c.score, 1) != currentProgramB_->currentData().toInt())
                c.score.versePrograms = {currentProgramA_->currentData().toInt(),
                                         currentProgramB_->currentData().toInt()};
        }
        else
        {
            c.primaryStaffProgram = currentProgramA_->currentData().toInt();
            c.otherStaffProgram = currentProgramB_->currentData().toInt();
        }
        c.mix = {melody_->isChecked(), accompaniment_->isChecked(),
                 selectedNumber(currentMelodyVolume_, c.mix.melodyVolume),
                 selectedNumber(currentAccompanimentVolume_, c.mix.accompanimentVolume)};
        c.playbackSource = source_->currentIndex();
        c.accompanimentPattern = pattern_->currentIndex();
        c.originalSpeed = selectedNumber(currentOriginalSpeed_, c.originalSpeed);
        c.originalVolume = selectedNumber(currentOriginalVolume_, c.originalVolume);
    }
    try
    {
        validateAppSettings(s, &initial_);
        if (!applyChanges || !applyChanges(s, c))
            return false;
        initial_ = s;
        context_ = c;
        for (auto *box : findChildren<QDoubleSpinBox *>())
            box->setProperty("initialDisplayedValue", box->value());
        error_->clear();
        error_->hide();
        refreshCapabilities();
        return true;
    }
    catch (const SettingsValidationError &error)
    {
        showError(QString::fromUtf8(error.what()), error.field());
        return false;
    }
    catch (const std::exception &error)
    {
        const QString message = QString::fromUtf8(error.what());
        const char *field = message == trText("messages.options.source_missing")  ? "optionsCurrentSource"
                            : message == trText("messages.options.score_invalid") ? "optionsCurrentTempo"
                                                                                  : nullptr;
        showError(message, field);
        return false;
    }
}
void OptionsDialog::showError(const QString &message, const char *fieldName)
{
    QWidget *field = fieldName ? findChild<QWidget *>(fieldName) : nullptr;
    if (field)
        for (int i = 0; i < tabs_->count(); ++i)
            if (tabs_->widget(i)->isAncestorOf(field))
                tabs_->setCurrentIndex(i);
    auto *scroll = qobject_cast<QScrollArea *>(tabs_->currentWidget());
    auto *form = qobject_cast<QFormLayout *>(scroll->widget()->layout());
    if (errorForm_)
    {
        const auto row = errorForm_->takeRow(error_);
        delete row.labelItem;
        delete row.fieldItem;
    }
    int errorRow = 0;
    for (int row = 0; row < form->rowCount(); ++row)
        if (containsField(form->itemAt(row, QFormLayout::FieldRole), field) ||
            containsField(form->itemAt(row, QFormLayout::SpanningRole), field))
        {
            errorRow = row + 1;
            break;
        }
    error_->setParent(scroll->widget());
    form->insertRow(errorRow, error_);
    errorForm_ = form;
    error_->setText(message);
    error_->setProperty("errorField", fieldName ? QString::fromLatin1(fieldName) : QString{});
    error_->show();
    if (field)
    {
        field->setFocus(Qt::OtherFocusReason);
        if (auto *edit = qobject_cast<QLineEdit *>(field))
            edit->selectAll();
    }
    scroll->ensureWidgetVisible(error_);
}

void OptionsDialog::refreshCapabilities()
{
    auto settings = initial_;
    settings.gmSoundFontPath = gm_->text().trimmed();
    settings.whisperModel = whisper_->text().trimmed();
    const auto status = inspectCapabilities(settings);
    for (const auto &item :
         {std::pair{pianoStatus_, &status.piano}, std::pair{gmStatus_, &status.gm},
          std::pair{whisperStatus_, &status.whisper}, std::pair{separationStatus_, &status.separation}})
    {
        item.first->setText(capabilityStatusText(*item.second));
        item.first->setToolTip(item.second->checkedFiles.join('\n'));
        item.first->setProperty("filesPresent", item.second->filesPresent);
    }
}
void OptionsDialog::selectPage(int index)
{
    tabs_->setCurrentIndex(index);
}
void OptionsDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslate();
}
void OptionsDialog::retranslate()
{
    outputBoost_->setToolTip(trText("ui.options.output_boost_help"));
    setWindowTitle(trText("ui.options.title"));
    const char *names[] = {"ui.options.general", "ui.options.audio", "ui.options.microphone", "ui.options.ai",
                           "ui.options.current"};
    for (int i = 0; i < 5; ++i)
        tabs_->setTabText(i, trText(names[i]));
    for (auto *object : findChildren<QObject *>())
    {
        const auto key = object->property("_ui_text").toByteArray();
        if (!key.isEmpty())
            object->setProperty("text", trText(key.constData()));
    }
    for (auto *combo : findChildren<QComboBox *>())
        for (int i = 0; i < combo->count(); ++i)
        {
            const auto key = combo->itemData(i, Qt::UserRole + 1).toByteArray();
            if (key.isEmpty())
                continue;
            QString name = trText(key.constData());
            for (const auto &argument : combo->itemData(i, Qt::UserRole + 2).toStringList())
                name = name.arg(argument);
            combo->setItemText(i, name);
        }
    auto *buttons = findChild<QDialogButtonBox *>("optionsButtons");
    buttons->button(QDialogButtonBox::Ok)->setText(trText("ui.button.ok"));
    buttons->button(QDialogButtonBox::Cancel)->setText(trText("ui.button.cancel"));
    buttons->button(QDialogButtonBox::Apply)->setText(trText("ui.options.apply"));
    refreshCapabilities();
}
} // namespace singlilt
