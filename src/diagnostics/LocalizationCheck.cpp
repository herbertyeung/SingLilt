// Language switching and widget-state preservation checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LocalizationCheck.h"
#include "i18n/LanguageManager.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <algorithm>
#include <memory>

namespace singlilt
{
namespace
{
struct State
{
    int stage = 0;
    QJsonObject report;
    QJsonObject score;
    QString userTitle, draftA, draftB, importChinese;
    int volume = 0, verse = 0, programA = 0, programB = 0;
    qint64 tick = 0;
    bool dirty = false;
    bool finished = false;
};
void writeReport(const QCommandLineParser &args, QApplication &app, QJsonObject &report, bool pass)
{
    report["passed"] = pass;
    if (args.isSet("report"))
    {
        QFile file(args.value("report"));
        if (!file.open(QIODevice::WriteOnly))
        {
            app.exit(4);
            return;
        }
        file.write(QJsonDocument(report).toJson());
    }
    app.exit(pass ? 0 : 3);
}
QJsonObject popupPixels(QComboBox *combo, const QString &path)
{
    auto *view = combo->view();
    auto image = view->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    const auto rect = view->visualRect(view->currentIndex());
    const double dpr = image.devicePixelRatio();
    QRect region(int((rect.x() + 3) * dpr), int((rect.y() + 2) * dpr), int((rect.width() - 6) * dpr),
                 int((rect.height() - 4) * dpr));
    region = region.intersected(image.rect());
    int blue = 0, white = 0;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
        {
            const auto c = image.pixelColor(x, y);
            if (c.blue() > 180 && c.red() < 110 && c.green() < 175)
                ++blue;
            if (c.red() > 235 && c.green() > 235 && c.blue() > 235)
                ++white;
        }
    const auto text = view->currentIndex().data(Qt::DisplayRole).toString();
    bool paletteContrast = true;
    for (auto group : {QPalette::Active, QPalette::Inactive})
        paletteContrast &= view->palette().color(group, QPalette::Highlight) !=
                           view->palette().color(group, QPalette::HighlightedText);
    return {{"text", text},
            {"selectedBluePixels", blue},
            {"selectedWhiteTextPixels", white},
            {"paletteContrast", paletteContrast},
            {"screenshotSaved", image.save(path)},
            {"passed", !text.isEmpty() && blue > 50 && white > 5 && paletteContrast}};
}
void selectPopupRow(QComboBox *combo, int row)
{
    combo->showPopup();
    auto *view = combo->view();
    const auto index = combo->model()->index(row, combo->modelColumn());
    view->setCurrentIndex(index);
    view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->viewport()->repaint();
}

bool saveCloudDialogScreenshot(QDialog &dialog, QSpinBox *timeout, const QString &path, int &maskedFields)
{
    auto capture = dialog.grab();
    QPainter painter(&capture);
    maskedFields = 0;
    for (auto *editor : dialog.findChildren<QLineEdit *>())
    {
        if (timeout && timeout->isAncestorOf(editor))
            continue;
        // Mask widget pixels without reading credentials, endpoint or model text.
        const QRect rectangle(editor->mapTo(&dialog, QPoint()), editor->size());
        painter.fillRect(rectangle, QColor("#E4E9F1"));
        ++maskedFields;
    }
    painter.end();
    return maskedFields >= 3 && capture.save(path);
}

QJsonObject checkCloudDialog(MainWindow &window, LanguageManager &languages, const QString &locale,
                             const QString &screenshot)
{
    auto report = std::make_shared<QJsonObject>();
    const auto scoreBefore = scoreToJson(window.project().score);
    const bool dirtyBefore = window.hasUnsavedChanges();
    auto *editorA = window.findChild<QLineEdit *>("lyricAEditor");
    auto *editorB = window.findChild<QLineEdit *>("lyricBEditor");
    const QString draftA = editorA ? editorA->text() : QString();
    const QString draftB = editorB ? editorB->text() : QString();
    (*report)["localeChanged"] = languages.setLanguage(locale);
    QPushButton *cloud = nullptr;
    for (auto *button : window.findChildren<QPushButton *>())
        if (button->property("_ui_text").toByteArray() == "ui.inspector.cloud")
        {
            cloud = button;
            break;
        }
    (*report)["buttonFound"] = cloud && cloud->isEnabled();
    if (cloud && cloud->isEnabled())
    {
        // This stack context cancels an un-fired callback if click() returns
        // without opening the modal. Captures contain no references to locals.
        QObject callbackLifetime;
        const QPointer<MainWindow> target(&window);
        const QString expectedLabel = trText("ui.dialog.vision_timeout");
        QTimer::singleShot(
            125, &callbackLifetime,
            [target, report, screenshot, expectedLabel]
            {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                const bool owned = target && dialog && dialog->parentWidget() == target.data();
                (*report)["modalOpened"] = owned;
                if (!owned)
                    return;
                auto *timeout = dialog->findChild<QSpinBox *>("visionTimeout");
                (*report)["timeoutControlFound"] = timeout != nullptr;
                if (timeout)
                {
                    (*report)["timeoutMinimum"] = timeout->minimum();
                    (*report)["timeoutMaximum"] = timeout->maximum();
                    (*report)["timeoutValue"] = timeout->value();
                    (*report)["timeoutRangeValid"] = timeout->minimum() == VisionConfig::MinTimeoutSeconds &&
                                                     timeout->maximum() == VisionConfig::MaxTimeoutSeconds;
                    (*report)["timeoutValueValid"] =
                        timeout->value() >= timeout->minimum() && timeout->value() <= timeout->maximum();
                }
                bool translatedLabel = false;
                for (const auto *label : dialog->findChildren<QLabel *>())
                    translatedLabel |= label->text() == expectedLabel && !expectedLabel.startsWith("ui.");
                (*report)["timeoutLabel"] = expectedLabel;
                (*report)["timeoutLabelTranslated"] = translatedLabel;
                int maskedFields = 0;
                (*report)["screenshotSaved"] =
                    saveCloudDialogScreenshot(*dialog, timeout, screenshot, maskedFields);
                (*report)["privateFieldsMasked"] = maskedFields;
                dialog->reject(); // Never click the send/accept button.
                (*report)["dialogRejected"] = dialog->result() == QDialog::Rejected;
            });
        cloud->click();
    }
    (*report)["scorePreserved"] = scoreBefore == scoreToJson(window.project().score);
    (*report)["dirtyPreserved"] = dirtyBefore == window.hasUnsavedChanges();
    (*report)["draftsPreserved"] = editorA && editorB && draftA == editorA->text() && draftB == editorB->text();
    (*report)["noRecognitionStarted"] = !window.isRecognizing();
    bool passed = true;
    for (const auto *key :
         {"localeChanged", "buttonFound", "modalOpened", "timeoutControlFound", "timeoutRangeValid",
          "timeoutValueValid", "timeoutLabelTranslated", "screenshotSaved", "dialogRejected", "scorePreserved",
          "dirtyPreserved", "draftsPreserved", "noRecognitionStarted"})
        passed &= (*report)[key].toBool();
    (*report)["passed"] = passed;
    return *report;
}

bool localizationPassed(const QJsonObject &report)
{
    bool passed = report["catalogErrors"].toArray().isEmpty();
    for (const auto *key :
         {"playingBeforeSwitch", "englishSwitch", "playingAfterSwitch", "positionPreserved", "captionChanged",
          "scoreUnchanged", "userTitleUnchanged", "draftsPreserved", "dirtyPreserved", "controlsPreserved",
          "chineseRestored", "draftsStillPreserved", "unsupportedLanguagePreserved"})
        passed &= report[key].toBool();
    for (const auto *key : {"englishPopup", "chinesePopup", "englishCloudDialog", "chineseCloudDialog"})
        passed &= report[key].toObject()["passed"].toBool();
    return passed;
}
} // namespace

void runLocalizationCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                          QApplication &app)
{
    auto state = std::make_shared<State>();
    auto *gate = new QTimer(&window);
    auto *deadline = new QTimer(&window);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, &window,
                     [state, &args, &app, gate]
                     {
                         if (state->finished)
                             return;
                         state->finished = true;
                         gate->stop();
                         state->report["error"] = "Localization UI check timed out";
                         writeReport(args, app, state->report, false);
                     });
    deadline->start(30000);
    QObject::connect(gate, &QTimer::timeout, &window,
                     [&window, &languages, &args, &app, state, gate, deadline]
                     {
                         if (state->finished)
                             return;
                         if (window.isRecognizing() || window.isAudioLoading())
                             return;
                         auto *verse = window.findChild<QComboBox *>("verseSelector");
                         auto *programA = window.findChild<QComboBox *>("programA");
                         auto *programB = window.findChild<QComboBox *>("programB");
                         auto *editorA = window.findChild<QLineEdit *>("lyricAEditor");
                         auto *editorB = window.findChild<QLineEdit *>("lyricBEditor");
                         auto *volume = window.findChild<QSlider *>("outputVolume");
                         auto *import = window.findChild<QPushButton *>("import");
                         if (!verse || !programA || !programB || !editorA || !editorB || !volume || !import)
                         {
                             gate->stop();
                             deadline->stop();
                             state->report["error"] = "Expected UI controls missing";
                             writeReport(args, app, state->report, false);
                             return;
                         }
                         const auto folder = QFileInfo(args.value("report")).absolutePath();
                         if (state->stage == 0)
                         {
                             state->report["catalogErrors"] =
                                 QJsonArray::fromStringList(languages.validateCatalogs());
                             languages.setLanguage("zh_CN");
                             state->importChinese = import->text();
                             programA->setCurrentIndex((programA->currentIndex() + 1) % programA->count());
                             if (verse->count() > 1)
                                 verse->setCurrentIndex(1);
                             volume->setValue(37);
                             editorA->setText("未保存的 A / unsaved A");
                             editorB->setText("未保存的 B / unsaved B");
                             state->draftA = editorA->text();
                             state->draftB = editorB->text();
                             state->score = scoreToJson(window.project().score);
                             state->userTitle = QString::fromStdString(window.project().score.title);
                             state->dirty = window.hasUnsavedChanges();
                             state->volume = volume->value();
                             state->verse = verse->currentIndex();
                             state->programA = programA->currentData().toInt();
                             state->programB = programB->currentData().toInt();
                             window.player().setAudioBackend(AudioBackend::WindowsMidi);
                             if (auto *backend = window.findChild<QComboBox *>("audioBackend"))
                             {
                                 QSignalBlocker blocker(backend);
                                 backend->setCurrentIndex(1);
                             }
                             auto *play = window.findChild<QPushButton *>("play");
                             if (play)
                                 play->click();
                             state->stage = 1;
                             return;
                         }
                         if (state->stage == 1)
                         {
                             state->tick = window.player().positionTicks();
                             state->report["playingBeforeSwitch"] = window.player().isPlaying();
                             state->report["englishSwitch"] = languages.setLanguage("en_US");
                             state->stage = 2;
                             return;
                         }
                         if (state->stage == 2)
                         {
                             state->report["playingAfterSwitch"] = window.player().isPlaying();
                             state->report["positionPreserved"] = window.player().positionTicks() > state->tick;
                             window.player().pause();
                             state->report["translatedImport"] = import->text();
                             state->report["captionChanged"] =
                                 import->text() != state->importChinese && !import->text().contains("ui.");
                             state->report["scoreUnchanged"] = state->score == scoreToJson(window.project().score);
                             state->report["userTitleUnchanged"] =
                                 state->userTitle == QString::fromStdString(window.project().score.title);
                             state->report["draftsPreserved"] =
                                 state->draftA == editorA->text() && state->draftB == editorB->text();
                             state->report["dirtyPreserved"] = state->dirty && window.hasUnsavedChanges();
                             state->report["controlsPreserved"] =
                                 volume->value() == state->volume && verse->currentIndex() == state->verse &&
                                 programA->currentData().toInt() == state->programA &&
                                 programB->currentData().toInt() == state->programB;
                             window.grab().save(folder + "/english-ui.png");
                             selectPopupRow(verse, 0);
                             state->stage = 3;
                             return;
                         }
                         if (state->stage == 3)
                         {
                             state->report["englishPopup"] = popupPixels(verse, folder + "/english-popup.png");
                             verse->hidePopup();
                             languages.setLanguage("zh_CN");
                             state->stage = 4;
                             return;
                         }
                         if (state->stage == 4)
                         {
                             // Retranslation updates the combo model through a queued LanguageChange event.
                             selectPopupRow(verse, 1);
                             state->stage = 5;
                             return;
                         }
                         if (state->stage == 5)
                         {
                             state->report["chinesePopup"] = popupPixels(verse, folder + "/chinese-popup.png");
                             verse->hidePopup();
                             state->report["chineseRestored"] = import->text() == state->importChinese;
                             state->report["draftsStillPreserved"] =
                                 state->draftA == editorA->text() && state->draftB == editorB->text();
                             window.grab().save(folder + "/chinese-ui.png");
                             const auto before = languages.language();
                             state->report["unsupportedLanguagePreserved"] =
                                 !languages.setLanguage("invalid-locale") && languages.language() == before;
                             window.player().stop();
                             gate->stop();
                             state->stage = 6;
                             // Keep the deadline active through both nested modal event loops.
                             state->report["englishCloudDialog"] =
                                 checkCloudDialog(window, languages, "en_US", folder + "/cloud-dialog-en.png");
                             if (state->finished)
                                 return;
                             state->report["chineseCloudDialog"] =
                                 checkCloudDialog(window, languages, "zh_CN", folder + "/cloud-dialog-zh.png");
                             if (state->finished)
                                 return;
                             deadline->stop();
                             state->finished = true;
                             writeReport(args, app, state->report, localizationPassed(state->report));
                         }
                     });
    gate->start(150);
}
} // namespace singlilt
