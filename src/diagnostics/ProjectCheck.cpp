// JPP persistence, corruption rejection, and file-action checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ProjectCheck.h"
#include "i18n/LanguageManager.h"
#include "storage/ProjectPackage.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/NotationRenderer.h"
#include "ui/ScoreView.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace singlilt
{
namespace
{
QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Read fixture failed");
    return file.readAll();
}
void writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Write fixture failed");
}
QByteArray digest(const QString &path)
{
    return QCryptographicHash::hash(readBytes(path), QCryptographicHash::Sha256).toHex();
}
void waveFixture(const QString &path, double frequency)
{
    QByteArray pcm;
    QDataStream samples(&pcm, QIODevice::WriteOnly);
    samples.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < 16000 * 16; ++i)
        samples << qint16(std::sin(i * frequency * 6.283185307179586 / 16000) * 4000);
    QByteArray wave;
    QDataStream stream(&wave, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(pcm.size() + 36);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000) << quint32(32000) << quint16(2)
           << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    wave += pcm;
    writeBytes(path, wave);
}
QJsonObject manifestOf(const QByteArray &bytes)
{
    const auto size = qFromBigEndian<quint32>(bytes.constData() + 8);
    return QJsonDocument::fromJson(bytes.mid(12, size)).object();
}
QByteArray changeManifest(const QByteArray &bytes, const std::function<void(QJsonObject &)> &change)
{
    const auto oldSize = qFromBigEndian<quint32>(bytes.constData() + 8);
    auto manifest = manifestOf(bytes);
    change(manifest);
    const auto json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const auto size = qToBigEndian(quint32(json.size()));
    return projectPackageMagic() + QByteArray(reinterpret_cast<const char *>(&size), 4) + json +
           bytes.mid(12 + oldSize);
}
class ProjectProbe final : public QObject
{
  public:
    ProjectProbe(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), languages_(languages), args_(args), app_(app),
          folder_(QFileInfo(args.value("report")).absolutePath())
    {
        folder_ += "/run-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(folder_);
        QTimer::singleShot(100, this, [this] { run(); });
    }

  private:
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ &= passed;
    }
    void rejects(const QString &name, const std::function<void()> &test)
    {
        bool rejected = false;
        try
        {
            test();
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(name, rejected);
    }
    void action(const char *name)
    {
        auto *action = window_.findChild<QAction *>(name);
        if (!action)
            throw std::runtime_error("File menu action missing");
        action->trigger();
    }
    void dialogAction(const char *name, const std::function<void(QDialog *)> &test)
    {
        QObject lifetime;
        QTimer::singleShot(100, &lifetime,
                           [&]
                           {
                               auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                               try
                               {
                                   if (!dialog)
                                       throw std::runtime_error("Expected modal dialog");
                                   test(dialog);
                               }
                               catch (const std::exception &error)
                               {
                                   check(QString::fromUtf8(error.what()), false);
                                   if (dialog)
                                       dialog->reject();
                               }
                           });
        action(name);
    }
    void choose(const char *name, const QString &path)
    {
        dialogAction(name,
                     [&](QDialog *dialog)
                     {
                         auto *file = qobject_cast<QFileDialog *>(dialog);
                         if (!file)
                             throw std::runtime_error("Expected project file dialog");
                         check("File dialog uses own project format",
                               file->nameFilters().first().contains("*.jpp"));
                         file->setDirectory(QFileInfo(path).absolutePath());
                         file->selectFile(path);
                         auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                         if (!name)
                             throw std::runtime_error("Filename input missing");
                         name->setText(path);
                         QMetaObject::invokeMethod(file, "accept");
                     });
    }
    void discard(const char *name, QMessageBox::StandardButton answer)
    {
        dialogAction(name,
                     [&](QDialog *dialog)
                     {
                         auto *message = qobject_cast<QMessageBox *>(dialog);
                         if (!message)
                             throw std::runtime_error("Expected unsaved confirmation");
                         message->button(answer)->click();
                     });
    }
    void run()
    {
        try
        {
            auto project = makePracticeScore();
            project.generatedNotation = true;
            project.score.versePrograms = {40, 73};
            project.score.notes.front().verseLyrics = {"A edited", ""};
            project.image = renderNumberedScore(project.score);
            project.accompaniment = generateAccompaniment(project.score);
            project.practiceMix = {true, true, .72, .43};
            ProjectPracticeSettings practice;
            practice.transpose = 2;
            practice.speed = .8;
            practice.metronome = false;
            practice.originalSpeed = .7;
            practice.originalVolume = .36;
            practice.loopEnabled = true;
            practice.loopStart = 480;
            practice.loopEnd = 1920;
            project.practiceSettings = practice;
            project.processing = {{"algorithm", "fixture"},
                                  {"separation", QJsonObject{{"model", "fixture-model"}}}};
            auto originals = std::make_shared<QTemporaryDir>(folder_ + "/originals-XXXXXX");
            if (!originals->isValid())
                throw std::runtime_error("Fixture directory failed");
            const QString input = args_.value("project-test-audio");
            const QString original = originals->filePath(input.isEmpty() ? "original.wav" : "original.mp3");
            if (input.isEmpty())
                waveFixture(original, 261.625565);
            else if (!QFile::copy(input, original))
                throw std::runtime_error("Copy source MP3 failed");
            const auto vocals = originals->filePath("vocals.wav");
            const auto instrumental = originals->filePath("instrumental.wav");
            waveFixture(vocals, 261.625565);
            waveFixture(instrumental, 130.812782);
            AudioSourceInfo audio;
            audio.path = original.toStdString();
            audio.vocalsPath = vocals.toStdString();
            audio.instrumentalPath = instrumental.toStdString();
            audio.vocalsSeparated = true;
            audio.separationModel = "fixture-model";
            audio.durationSeconds = 300;
            audio.selectedEndSeconds = buildTimeline(project.score).durationSeconds();
            audio.timingFingerprint = audioTimingFingerprint(project.score);
            std::int64_t tick = 0;
            for (size_t i = 0; i < project.score.notes.size(); ++i)
            {
                const auto end = tick + project.score.notes[i].durationTicks;
                audio.timings.push_back({int(i), double(tick) / 480 * 60 / project.score.bpm,
                                         double(end) / 480 * 60 / project.score.bpm, tick, end});
                tick = end;
            }
            audio.lyricTimings = {{"A edited", 0, .7, 0}};
            project.audioSource = audio;
            const auto originalHash = digest(original), vocalHash = digest(vocals),
                       instrumentalHash = digest(instrumental);
            const auto saved = folder_ + "/portable.jpp";
            saveProject(saved, project);
            const auto bytes = readBytes(saved);
            const auto manifest = manifestOf(bytes);
            check("Version4 container includes four hashed resources",
                  bytes.startsWith(projectPackageMagic()) && manifest.value("schemaVersion").toInt() == 4 &&
                      manifest.value("resources").toArray().size() == 4);
            check("Manifest has internal references, no original/cache paths",
                  !QJsonDocument(manifest).toJson().contains(originals->path().toUtf8()) &&
                      manifest.value("audioSource").toObject().value("path").toString() == "asset:original");
            check("Original MP3/WAV bytes unchanged by save",
                  digest(original) == originalHash && digest(vocals) == vocalHash);
            const auto moved = folder_ + "/relocated.jpp";
            check("Move only .jpp", QFile::copy(saved, moved));
            originals.reset();
            check("External originals removed from fixture", !QFileInfo::exists(original));
            auto restored = loadProject(moved);
            check("All audio bytes survive relocation",
                  digest(QString::fromStdString(restored.audioSource->path)) == originalHash &&
                      digest(QString::fromStdString(restored.audioSource->vocalsPath)) == vocalHash &&
                      digest(QString::fromStdString(restored.audioSource->instrumentalPath)) == instrumentalHash);
            check("Score, lyrics, anchors, A/B programs roundtrip",
                  scoreToJson(restored.score) == scoreToJson(project.score));
            check("Processing model, time mapping and mix survive",
                  restored.processing == project.processing &&
                      restored.audioSource->timings.size() == project.score.notes.size() &&
                      restored.audioSource->lyricTimings.size() == 1 &&
                      restored.audioSource->timingFingerprint == audioTimingFingerprint(restored.score) &&
                      restored.practiceMix.accompanimentVolume == .43 && restored.accompaniment &&
                      restored.accompaniment->melodyFingerprint == project.accompaniment->melodyFingerprint);
            check("Practice settings roundtrip",
                  restored.practiceSettings && restored.practiceSettings->transpose == 2 &&
                      restored.practiceSettings->loopStart == 480 && restored.practiceSettings->loopEnd == 1920 &&
                      restored.practiceSettings->speed == .8);
            const auto cache = restored.mediaDirectory->path();
            auto retained = restored;
            restored = {};
            check("Project copies retain extracted audio lifetime", QFileInfo::exists(cache));
            const auto resaved = folder_ + "/resaved.jpp";
            saveProject(resaved, retained);
            check("Relocated project can be re-saved",
                  digest(QString::fromStdString(loadProject(resaved).audioSource->path)) == originalHash);
            retained = {};
            check("Last Project releases temporary cache", !QFileInfo::exists(cache));
            const auto corrupt = folder_ + "/corrupt.jpp";
            auto damaged = bytes;
            damaged[damaged.size() - 1] ^= 1;
            writeBytes(corrupt, damaged);
            rejects("Corrupt asset checksum rejected", [&] { loadProject(corrupt); });
            writeBytes(corrupt, bytes.left(bytes.size() - 1));
            rejects("Truncated package rejected", [&] { loadProject(corrupt); });
            writeBytes(corrupt, bytes + "x");
            rejects("Trailing bytes rejected", [&] { loadProject(corrupt); });
            writeBytes(corrupt, changeManifest(bytes,
                                               [](QJsonObject &m)
                                               {
                                                   auto a = m["resources"].toArray();
                                                   auto e = a[0].toObject();
                                                   e["name"] = "../escape.png";
                                                   a[0] = e;
                                                   m["resources"] = a;
                                               }));
            rejects("Resource traversal metadata rejected", [&] { loadProject(corrupt); });
            writeBytes(corrupt, changeManifest(bytes,
                                               [](QJsonObject &m)
                                               {
                                                   auto a = m["resources"].toArray();
                                                   a.append(a[0]);
                                                   m["resources"] = a;
                                               }));
            rejects("Duplicate / excess resources rejected", [&] { loadProject(corrupt); });
            writeBytes(corrupt, changeManifest(bytes,
                                               [](QJsonObject &m)
                                               {
                                                   auto a = m["resources"].toArray();
                                                   auto e = a[0].toObject();
                                                   e["size"] = "4294967295";
                                                   a[0] = e;
                                                   m["resources"] = a;
                                               }));
            rejects("Oversized resource rejected before extraction", [&] { loadProject(corrupt); });
            writeBytes(corrupt, changeManifest(bytes,
                                               [](QJsonObject &m)
                                               {
                                                   auto a = m["audioSource"].toObject();
                                                   a["path"] = "C:/external.mp3";
                                                   m["audioSource"] = a;
                                               }));
            rejects("External reference in version4 rejected", [&] { loadProject(corrupt); });
            writeBytes(corrupt, changeManifest(bytes,
                                               [](QJsonObject &m)
                                               {
                                                   auto a = m["practiceSettings"].toObject();
                                                   a["speed"] = -1;
                                                   m["practiceSettings"] = a;
                                               }));
            rejects("Invalid practice setting rejected", [&] { loadProject(corrupt); });
            damaged = bytes;
            const quint32 excessive = qToBigEndian(quint32(0xffffffff));
            damaged.replace(8, 4, reinterpret_cast<const char *>(&excessive), 4);
            writeBytes(corrupt, damaged);
            rejects("Oversized manifest rejected", [&] { loadProject(corrupt); });
            const auto before = digest(saved);
            rejects("Missing media fails save", [&] { saveProject(saved, project); });
            check("Failed save preserves complete old project",
                  digest(saved) == before && loadProject(saved).audioSource.has_value());
            auto invalid = loadProject(saved);
            invalid.practiceSettings->speed = -1;
            rejects("Invalid write settings rejected", [&] { saveProject(saved, invalid); });
            check("Invalid metadata save preserves old bytes", digest(saved) == before);
            QBuffer png;
            png.open(QIODevice::WriteOnly);
            project.image.save(&png, "PNG");
            auto legacy = projectToJson(loadProject(saved));
            legacy.remove("audioSource");
            legacy.remove("practiceSettings");
            legacy.remove("processing");
            legacy.insert("imagePng", QString::fromLatin1(png.data().toBase64()));
            for (int schema = 1; schema <= 3; ++schema)
            {
                const auto old = folder_ + QString("/legacy%1.jpp").arg(schema);
                legacy["schemaVersion"] = schema;
                const auto legacyBytes = QJsonDocument(legacy).toJson();
                writeBytes(old, legacyBytes);
                const auto read = loadProject(old);
                check(QString("Read legacy schema%1").arg(schema),
                      read.score.notes.size() == project.score.notes.size());
                saveProject(old, read);
                check(QString("Upgrade schema%1 retains byte-identical backup").arg(schema),
                      readBytes(old + ".legacy.bak") == legacyBytes &&
                          readBytes(old).startsWith(projectPackageMagic()));
                saveProject(old, loadProject(old));
                check(QString("Re-save schema%1 upgrade keeps backup").arg(schema),
                      readBytes(old + ".legacy.bak") == legacyBytes);
            }
            // Exercise the real File actions and dialogs, not only storage helpers.
            action("menuNew");
            check("New creates editable blank score and dirty state", window_.project().score.notes.empty() &&
                                                                          !window_.project().image.isNull() &&
                                                                          window_.hasUnsavedChanges());
            const auto blank = folder_ + "/blank.jpp";
            choose("menuSave", blank);
            check("Blank project saves and reopens", QFileInfo::exists(blank) &&
                                                         loadProject(blank).score.notes.empty() &&
                                                         !window_.hasUnsavedChanges());
            action("menuNew");
            discard("menuNew", QMessageBox::Cancel);
            check("Cancel keeps unsaved document", window_.hasUnsavedChanges());
            discard("menuNew", QMessageBox::Discard);
            check("Discard switches to new blank document", window_.project().score.notes.empty());
            window_.setProject(loadProject(blank));
            action("menuCorrectionMode");
            auto *view = dynamic_cast<ScoreView *>(window_.findChild<QGraphicsView *>("scoreView"));
            if (!view)
                throw std::runtime_error("Score viewport missing");
            const QPoint point = view->mapFromScene(QPointF(100, 100));
            QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(point),
                                    QPointF(view->viewport()->mapToGlobal(point)), Qt::LeftButton, Qt::LeftButton,
                                    Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &doubleClick);
            // A click starts asynchronous audition; wait with an event-loop timer before save.
            QTimer::singleShot(1500, this, [this] { uiFinish(); });
        }
        catch (const std::exception &error)
        {
            check(QString::fromUtf8(error.what()), false);
            finish();
        }
    }
    void uiFinish()
    {
        try
        {
            check("Blank score adds and renders note",
                  window_.project().score.notes.size() == 1 && window_.project().generatedNotation);
            if (window_.project().score.notes.size() != 1)
                throw std::runtime_error("Blank score insertion failed; further note checks are invalid");
            auto *degree = window_.findChild<QComboBox *>("degree");
            if (!degree)
                degree = window_.findChild<QComboBox *>("noteDegree");
            if (!degree)
                throw std::runtime_error("Note editor missing");
            degree->setCurrentIndex(degree->findData(3));
            window_.findChild<QLineEdit *>("lyricAEditor")->setText("edited lyric");
            window_.findChild<QPushButton *>("applyNoteChanges")->click();
            auto context = window_.optionsContext();
            context.speed = .75;
            context.transpose = 3;
            context.score.versePrograms = {40, 73};
            window_.applyOptions(window_.appSettings(), context);
            dialogAction("menuSaveAs", [](QDialog *dialog) { dialog->reject(); });
            check("Cancel Save As retains dirty document", window_.hasUnsavedChanges());
            const auto edited = folder_ + "/edited.jpp";
            choose("menuSaveAs", edited);
            check("Save As stores corrected note and practice state",
                  loadProject(edited).score.notes[0].degree == 3 &&
                      loadProject(edited).score.notes[0].lyric == "edited lyric" && !window_.hasUnsavedChanges());
            const auto editedHash = digest(edited);
            const auto second = folder_ + "/edited-copy.jpp";
            choose("menuSaveAs", second);
            check("Save As leaves previous file unchanged",
                  digest(edited) == editedHash && QFileInfo::exists(second));
            window_.findChild<QComboBox *>("programA")
                ->setCurrentIndex(window_.findChild<QComboBox *>("programA")->findData(24));
            action("menuSave");
            check("Save uses current Save As destination",
                  digest(edited) == editedHash && loadProject(second).score.versePrograms[0] == 24);
            auto *program = window_.findChild<QComboBox *>("programA");
            program->setCurrentIndex(program->findData(25));
            discard("menuNew", QMessageBox::Save);
            check("Unsaved Save persists before New",
                  loadProject(second).score.versePrograms[0] == 25 && window_.project().score.notes.empty());
            discard("menuNew", QMessageBox::Discard);
            window_.setProject(loadProject(folder_ + "/blank.jpp"));
            choose("menuOpenProject", edited);
            check("Open restores editor and practice controls",
                  window_.project().score.notes[0].degree == 3 && window_.player().speed() == .75 &&
                      window_.player().transpose() == 3 &&
                      window_.findChild<QComboBox *>("programA")->currentData().toInt() == 40);
            check("Frequent controls remain on playback bar",
                  window_.findChild<QCheckBox *>("metronome")->isVisible() &&
                      window_.findChild<QComboBox *>("programA")->isVisible() &&
                      window_.findChild<QComboBox *>("programB")->isVisible());
            languages_.setLanguage("en_US");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
            check("English file actions", window_.findChild<QAction *>("menuNew")->text() == "New project" &&
                                              window_.findChild<QAction *>("menuSaveAs")->text() == "Save as…");
            languages_.setLanguage("zh_CN");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
            check("Chinese file actions", window_.findChild<QAction *>("menuOpenProject")->text() == "打开工程…");
            auto withAudio = loadProject(folder_ + "/portable.jpp");
            window_.setProject(withAudio);
            auto *source = window_.findChild<QComboBox *>("playbackSource");
            if (!source)
                throw std::runtime_error("Playback source selector missing");
            if (!args_.isSet("diagnostic-no-audio"))
            {
                for (int type : {1, 2, 3})
                {
                    source->setCurrentIndex(source->findData(type));
                    check(QString("Packaged audio source%1 opens through native player").arg(type),
                          window_.originalAudioPlayer().isOpen());
                }
            }
            source->setCurrentIndex(source->findData(0));
            window_.grab().save(folder_ + "/project-ui.png");
            window_.findChild<QMenu *>("fileMenu")->popup(window_.mapToGlobal(QPoint(10, 30)));
            QTimer::singleShot(100, this,
                               [this]
                               {
                                   window_.findChild<QMenu *>("fileMenu")->grab().save(folder_ + "/file-menu.png");
                                   window_.findChild<QMenu *>("fileMenu")->hide();
                                   finish();
                               });
            return;
        }
        catch (const std::exception &error)
        {
            check(QString::fromUtf8(error.what()), false);
        }
        finish();
    }
    void finish()
    {
        int passed = 0;
        for (const auto &entry : checks_)
            if (entry.toObject()["passed"].toBool())
                ++passed;
        QJsonObject report{{"passed", passed_},
                           {"checks", checks_},
                           {"passedCount", passed},
                           {"checkCount", checks_.size()},
                           {"version", QApplication::applicationVersion()},
                           {"audioOutputTested", !args_.isSet("diagnostic-no-audio")},
                           {"fixtureFolder", folder_},
                           {"audioFixtures", "original MP3/WAV plus generated PCM WAV stems"}};
        writeBytes(args_.value("report"), QJsonDocument(report).toJson());
        Project blank;
        blank.image = QImage(640, 360, QImage::Format_RGB32);
        blank.image.fill(Qt::white);
        window_.setProject(std::move(blank));
        app_.exit(passed_ ? 0 : 1);
    }
    MainWindow &window_;
    LanguageManager &languages_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QString folder_;
    QJsonArray checks_;
    bool passed_ = true;
};
} // namespace
void runProjectCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                     QApplication &app)
{
    new ProjectProbe(window, languages, args, app);
}
} // namespace singlilt
