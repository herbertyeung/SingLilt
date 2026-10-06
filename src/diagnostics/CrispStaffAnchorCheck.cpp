// Native-OMR source-anchor detection regression fixtures.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "CrispStaffAnchorCheck.h"
#include "recognition/CrispStaffSourceAnchors.h"
#include <QJsonArray>
#include <QPainter>
#include <array>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
constexpr double Spacing = 16;
constexpr double UpperBottom = 154;
constexpr double LowerBottom = 324;

QImage staffPage()
{
    QImage page(700, 500, QImage::Format_RGB32);
    page.fill(Qt::white);
    QPainter painter(&page);
    painter.setPen(QPen(Qt::black, 2));
    for (const double bottom : {UpperBottom, LowerBottom})
        for (int line = 0; line < 5; ++line)
            painter.drawLine(QPointF(30, bottom - line * Spacing), QPointF(670, bottom - line * Spacing));
    return page;
}

void drawHead(QImage &page, double x, int staff, int step, bool hollow)
{
    const double bottom = staff == 1 ? UpperBottom : LowerBottom;
    const double y = bottom - step * Spacing / 2;
    QPainter painter(&page);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::black, 2));
    for (int ledger = step < 0 ? -2 : 10; step < 0 ? ledger >= step : ledger <= step; ledger += step < 0 ? -2 : 2)
        painter.drawLine(QPointF(x - 15, bottom - ledger * Spacing / 2),
                         QPointF(x + 15, bottom - ledger * Spacing / 2));
    painter.save();
    painter.translate(x, y);
    painter.rotate(-20);
    painter.setBrush(hollow ? Qt::white : Qt::black);
    painter.drawEllipse(QRectF(-Spacing * 0.62, -Spacing * 0.36, Spacing * 1.24, Spacing * 0.72));
    painter.restore();
    painter.drawLine(QPointF(x + Spacing * 0.58, y), QPointF(x + Spacing * 0.58, y - Spacing * 3));
}

struct Fixture
{
    MusicXmlImportResult imported;
    Score score;
    StaffPerformance performance;

    Fixture()
    {
        MusicXmlPart part;
        part.id = "P1";
        part.staffCount = 2;
        MusicXmlAttributes attributes;
        attributes.clefs = {{1, "G", 2, 0}, {2, "F", 4, 0}};
        part.attributes.push_back(attributes);
        imported.parts.push_back(part);
        for (int staff = 1; staff <= 2; ++staff)
        {
            MusicXmlTrack track;
            track.partId = "P1";
            track.staff = staff;
            imported.tracks.push_back(track);
        }
    }

    void add(int staff, QString step, int octave, int pitch, std::int64_t start,
             std::int64_t duration = TicksPerQuarter, int page = 0, int guide = -1)
    {
        MusicXmlNoteEvent event;
        event.startTick = start;
        event.endTick = start + duration;
        event.step = step;
        event.octave = octave;
        event.midiPitch = pitch;
        event.pageIndex = page;
        event.type = duration >= 2 * TicksPerQuarter ? "half" : "quarter";
        imported.tracks[static_cast<std::size_t>(staff - 1)].events.push_back(event);
        StaffPerformanceNote note;
        note.startTick = start;
        note.durationTicks = duration;
        note.midiPitch = pitch;
        note.staff = staff;
        note.voice = "P1:1";
        note.pageIndex = page;
        note.sourceNoteIndex = guide;
        performance.notes.push_back(note);
    }
};

bool emptyAnchors(const StaffPerformance &performance)
{
    for (const auto &note : performance.notes)
        if (note.hasImageAnchor || note.source.x != 0 || note.source.y != 0 || note.source.width != 0 ||
            note.source.height != 0)
            return false;
    return true;
}
} // namespace

QJsonObject crispStaffAnchorDiagnosticReport()
{
    QJsonArray checks;
    QString error;
    const auto check = [&](const QString &name, bool passed)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}});
        if (!passed)
            throw std::runtime_error(name.toStdString());
    };
    try
    {
        Fixture basic;
        basic.add(1, "C", 4, 60, 0, 480, 0, 0);
        basic.add(1, "E", 4, 64, 0, 480, 0, 0);
        basic.add(1, "C", 4, 60, 480, 480, 0, 1);
        basic.add(1, "D", 4, 62, 960, 960, 0, 2);
        basic.add(2, "A", 2, 45, 0);
        basic.add(2, "C", 3, 48, 480, 960);
        for (const auto &pitch : {std::pair{3, 480}, std::pair{1, 480}, std::pair{2, 960}})
        {
            Note guide;
            guide.degree = pitch.first;
            guide.durationTicks = pitch.second;
            basic.score.notes.push_back(guide);
        }
        auto image = staffPage();
        drawHead(image, 140, 1, -2, false);
        drawHead(image, 140, 1, 0, false);
        drawHead(image, 280, 1, -2, false);
        drawHead(image, 440, 1, -1, true);
        drawHead(image, 140, 2, 1, false);
        drawHead(image, 350, 2, 3, true);
        const auto basicReport =
            applyCrispStaffSourceAnchors({image}, basic.imported, basic.score, basic.performance);
        checks.append(QJsonObject{{"name", "basic detection counts"},
                                  {"anchored", basicReport.anchoredNotes},
                                  {"missing", basicReport.unanchoredNotes},
                                  {"warnings", basicReport.warnings.join("; ")}});
        for (const auto &note : basic.performance.notes)
            checks.append(QJsonObject{{"name", "basic note"},
                                      {"pitch", note.midiPitch},
                                      {"start", double(note.startTick)},
                                      {"staff", note.staff},
                                      {"anchored", note.hasImageAnchor},
                                      {"x", note.source.x},
                                      {"y", note.source.y}});
        check("Filled/hollow heads, ledger notes, both staves and same-x chord match real ink",
              basicReport.error.isEmpty() && basicReport.anchoredNotes == 6);
        check("Only the selected pitch supplies the practice guide", basicReport.anchoredGuideNotes == 3);
        const std::array<QPointF, 6> centers{
            {{140, 170}, {140, 154}, {280, 170}, {440, 162}, {140, 316}, {350, 300}}};
        for (std::size_t index = 0; index < centers.size(); ++index)
        {
            const auto &box = basic.performance.notes[index].source;
            check(QString("Detected head %1 bounds contain the painted head center").arg(index),
                  box.width >= 8 && box.height >= 5 && std::abs(box.x + box.width / 2 - centers[index].x()) < 5 &&
                      std::abs(box.y + box.height / 2 - centers[index].y()) < 5);
        }

        Fixture repeated;
        for (int index = 0; index < 3; ++index)
            repeated.add(1, "E", 4, 64, index * 480);
        auto missing = staffPage();
        drawHead(missing, 150, 1, 0, false);
        drawHead(missing, 450, 1, 0, false);
        const auto missingReport =
            applyCrispStaffSourceAnchors({missing}, repeated.imported, repeated.score, repeated.performance);
        check("A missing repeated head leaves every ambiguous optimum unanchored",
              missingReport.error.isEmpty() && missingReport.anchoredNotes == 0 &&
                  emptyAnchors(repeated.performance) && repeated.performance.notes.size() == 3);
        drawHead(missing, 300, 1, 0, false);
        const auto completeRepeated =
            applyCrispStaffSourceAnchors({missing}, repeated.imported, repeated.score, repeated.performance);
        check("Complete repeated pitches retain their unique left-to-right sequence",
              completeRepeated.error.isEmpty() && completeRepeated.anchoredNotes == 3);

        Fixture whole;
        whole.add(1, "E", 4, 64, 0, 1920);
        whole.imported.tracks.front().events.front().type = "whole";
        auto wholeImage = staffPage();
        {
            QPainter painter(&wholeImage);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(Qt::black, 2));
            painter.setBrush(Qt::white);
            painter.drawEllipse(
                QRectF(350 - Spacing * 0.73, UpperBottom - Spacing * 0.42, Spacing * 1.46, Spacing * 0.84));
        }
        const auto wholeReport =
            applyCrispStaffSourceAnchors({wholeImage}, whole.imported, whole.score, whole.performance);
        check("A horizontal hollow whole-note head does not require a stem",
              wholeReport.error.isEmpty() && wholeReport.anchoredNotes == 1);

        Fixture changed;
        changed.add(1, "E", 4, 64, 0);
        changed.add(1, "G", 3, 55, 480);
        MusicXmlAttributes bass;
        bass.startTick = 480;
        bass.clefs = {{1, "F", 4, 0}};
        changed.imported.parts.front().attributes.push_back(bass);
        auto clefImage = staffPage();
        drawHead(clefImage, 150, 1, 0, false);
        drawHead(clefImage, 450, 1, 7, false);
        const auto changedReport =
            applyCrispStaffSourceAnchors({clefImage}, changed.imported, changed.score, changed.performance);
        check("An explicit mid-stream treble/bass change uses the written pitch at that tick",
              changedReport.error.isEmpty() && changedReport.anchoredNotes == 2);

        Fixture multi;
        multi.add(1, "E", 4, 64, 0, 480, 0);
        multi.add(2, "C", 3, 48, 0, 480, 0);
        multi.add(1, "F", 4, 65, 480, 480, 1);
        multi.add(2, "D", 3, 50, 480, 480, 1);
        auto first = staffPage(), second = staffPage();
        drawHead(first, 150, 1, 0, false);
        drawHead(first, 250, 2, 3, false);
        drawHead(second, 450, 1, 1, false);
        drawHead(second, 550, 2, 4, false);
        const auto multiReport =
            applyCrispStaffSourceAnchors({first, second}, multi.imported, multi.score, multi.performance);
        check("Multiple pages use their own pixels and retain page indices",
              multiReport.error.isEmpty() && multiReport.anchoredNotes == 4 &&
                  multi.performance.notes[2].pageIndex == 1 && multi.performance.notes[2].source.x > 400);

        Fixture partial;
        partial.add(1, "E", 4, 64, 0);
        partial.add(2, "C", 3, 48, 0);
        auto upperSystem = staffPage(), lowerSystem = staffPage();
        drawHead(upperSystem, 150, 1, 0, false);
        drawHead(upperSystem, 150, 2, 3, false);
        drawHead(lowerSystem, 450, 1, 2, false);
        drawHead(lowerSystem, 450, 2, 5, false);
        QImage wholePage(700, 1000, QImage::Format_RGB32);
        wholePage.fill(Qt::white);
        {
            QPainter painter(&wholePage);
            painter.drawImage(0, 0, upperSystem);
            painter.drawImage(0, 500, lowerSystem);
        }
        const auto partialReport =
            applyCrispStaffSourceAnchors({wholePage}, partial.imported, partial.score, partial.performance);
        check("A first-system-only stream does not report the remaining system as covered",
              partialReport.error.isEmpty() && partialReport.detectedSystems == 2 &&
                  partialReport.coveredSystems == 1 && partialReport.unmatchedDetectedNoteheads >= 2);
        const auto regions = crispStaffSystemRegions(wholePage);
        check("System regions follow detected paired staves and remain inside the original image",
              regions.size() == 2 && wholePage.rect().contains(regions[0]) &&
                  wholePage.rect().contains(regions[1]) && !regions[0].intersects(regions[1]));

        QImage blank(700, 500, QImage::Format_RGB32);
        blank.fill(Qt::white);
        const auto blankReport =
            applyCrispStaffSourceAnchors({blank}, basic.imported, basic.score, basic.performance);
        check("Blank pixels clear stale source boxes without changing pitch or sound duration",
              blankReport.anchoredNotes == 0 && emptyAnchors(basic.performance) &&
                  basic.performance.notes.front().midiPitch == 60 &&
                  basic.performance.notes.front().durationTicks == 480);
        std::atomic_bool cancellation{true};
        const auto cancelled =
            applyCrispStaffSourceAnchors({image}, basic.imported, basic.score, basic.performance, &cancellation);
        check("Cancellation returns an explicit error and no stale anchors",
              !cancelled.error.isEmpty() && emptyAnchors(basic.performance));
    }
    catch (const std::exception &exception)
    {
        error = QString::fromUtf8(exception.what());
    }
    return {{"passed", error.isEmpty()}, {"error", error}, {"checks", checks}};
}
} // namespace singlilt
