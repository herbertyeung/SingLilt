// Full-voice staff corrections before applying an edited score.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffCorrectionDialog.h"
#include "ScoreView.h"
#include "application/StaffScoreCorrection.h"
#include "i18n/LanguageManager.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
QTableWidgetItem *textItem(const QString &text, bool editable = true)
{
    auto *item = new QTableWidgetItem(text);
    if (!editable)
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QTableWidgetItem *checkItem(bool checked)
{
    auto *item = new QTableWidgetItem;
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return item;
}

QString requiredText(const QTableWidget &table, int row, int column)
{
    const auto *item = table.item(row, column);
    if (!item || item->text().trimmed().isEmpty())
        throw std::runtime_error(trText("ui.staff_correction.missing_fields").toStdString());
    return item->text().trimmed();
}

int integerCell(const QTableWidget &table, int row, int column)
{
    bool valid = false;
    const int number = requiredText(table, row, column).toInt(&valid);
    if (!valid)
        throw std::runtime_error(trText("ui.staff_correction.invalid_integer").toStdString());
    return number;
}

std::int64_t beatCell(const QTableWidget &table, int row, int column)
{
    bool valid = false;
    const double beats = requiredText(table, row, column).toDouble(&valid);
    if (!valid)
        throw std::runtime_error(trText("ui.staff_correction.invalid_beats").toStdString());
    return staffCorrectionTicks(beats);
}

QString beatText(std::int64_t ticks)
{
    return QString::number(double(ticks) / TicksPerQuarter, 'g', 15);
}
} // namespace

StaffCorrectionDialog::StaffCorrectionDialog(Project project, QWidget *parent)
    : QDialog(parent), original_(std::move(project))
{
    if (!original_.processing.value("local").toBool() || !original_.staffPerformance ||
        original_.score.writtenMeasures.empty())
        throw std::runtime_error(trText("ui.staff_correction.local_only").toStdString());
    setObjectName("staffCorrectionDialog");
    setWindowTitle(trText("ui.staff_correction.title"));
    resize(1360, 860);
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(trText("ui.staff_correction.help"), this);
    help->setWordWrap(true);
    help->setTextFormat(Qt::PlainText);
    layout->addWidget(help);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *tabs = new QTabWidget(splitter);
    notes_ = new QTableWidget(0, original_.staffImagePlayback ? 14 : 9, tabs);
    notes_->setObjectName("staffCorrectionNotes");
    notes_->setHorizontalHeaderLabels(
        {trText("ui.staff_correction.midi"), trText("ui.staff_correction.measure_index"),
         trText("ui.staff_correction.onset"), trText("ui.staff_correction.duration"),
         trText("ui.staff_correction.staff"), trText("ui.staff_correction.voice"),
         trText("ui.staff_correction.tie_start"), trText("ui.staff_correction.tie_stop"),
         trText("ui.staff_correction.unresolved")});
    if (original_.staffImagePlayback)
    {
        const QStringList anchorTitles{trText("ui.original_playback.anchor"), "X", "Y", "W", "H"};
        for (int column = 9; column < 14; ++column)
            notes_->setHorizontalHeaderItem(column, new QTableWidgetItem(anchorTitles[column - 9]));
    }
    notes_->setSelectionBehavior(QAbstractItemView::SelectRows);
    notes_->setSelectionMode(QAbstractItemView::SingleSelection);
    notes_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    notes_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    notes_->setAlternatingRowColors(true);
    notes_->setSortingEnabled(false);
    tabs->addTab(notes_, trText("ui.staff_correction.notes"));
    measures_ = new QTableWidget(int(original_.score.writtenMeasures.size()), 6, tabs);
    measures_->setObjectName("staffCorrectionMeasures");
    measures_->setHorizontalHeaderLabels(
        {trText("ui.staff_correction.measure_index"), trText("ui.staff_correction.printed_number"),
         trText("ui.staff_correction.numerator"), trText("ui.staff_correction.denominator"),
         trText("ui.staff_correction.measure_duration"), trText("ui.staff_correction.page")});
    measures_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int row = 0; row < measures_->rowCount(); ++row)
    {
        const auto &measure = original_.score.writtenMeasures[std::size_t(row)];
        measures_->setItem(row, 0, textItem(QString::number(row + 1), false));
        measures_->setItem(row, 1, textItem(QString::number(measure.number + 1)));
        measures_->setItem(row, 2, textItem(QString::number(measure.beatsPerBar)));
        measures_->setItem(row, 3, textItem(QString::number(measure.beatUnit)));
        measures_->setItem(row, 4, textItem(beatText(measure.durationTicks)));
        measures_->setItem(row, 5, textItem(QString::number(measure.pageIndex + 1), false));
    }
    tabs->addTab(measures_, trText("ui.staff_correction.measures"));
    auto *source = new QWidget(splitter);
    auto *sourceLayout = new QVBoxLayout(source);
    auto *sourceTitle = new QLabel(trText("ui.staff_correction.original"), source);
    sourceLayout->addWidget(sourceTitle);
    sourcePage_ = new QComboBox(source);
    sourcePage_->setObjectName("staffCorrectionSourcePage");
    for (std::size_t page = 0; page < original_.staffPages.size(); ++page)
        sourcePage_->addItem(original_.staffPages[page].label, int(page));
    sourceLayout->addWidget(sourcePage_);
    sourceView_ = new ScoreView(source);
    sourceView_->setObjectName("staffCorrectionSourceView");
    sourceLayout->addWidget(sourceView_, 1);
    connect(sourcePage_, &QComboBox::currentIndexChanged, this, [this](int index) { showSourcePage(index); });
    splitter->addWidget(tabs);
    splitter->addWidget(source);
    splitter->setSizes({860, 460});
    layout->addWidget(splitter, 1);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(trText("ui.staff_correction.add"), this);
    add->setObjectName("staffCorrectionAddNote");
    connect(add, &QPushButton::clicked, this, [this] { addNoteRow(nullptr, -1); });
    actions->addWidget(add);
    auto *remove = new QPushButton(trText("ui.staff_correction.remove"), this);
    remove->setObjectName("staffCorrectionDeleteNote");
    connect(remove, &QPushButton::clicked, this,
            [this]
            {
                bindingRow_ = -1;
                sourceView_->viewport()->unsetCursor();
                if (notes_->currentRow() >= 0)
                    notes_->removeRow(notes_->currentRow());
            });
    actions->addWidget(remove);
    if (original_.staffImagePlayback)
    {
        auto *bind = new QPushButton(trText("ui.original_playback.bind"), this);
        bind->setObjectName("staffCorrectionBindSource");
        actions->addWidget(bind);
        connect(bind, &QPushButton::clicked, this,
                [this]
                {
                    bindingRow_ = notes_->currentRow();
                    if (bindingRow_ < 0)
                        return;
                    try
                    {
                        const int measure = integerCell(*notes_, bindingRow_, 1) - 1;
                        if (measure < 0 || std::size_t(measure) >= original_.score.writtenMeasures.size())
                            throw std::runtime_error(trText("ui.staff_correction.invalid_position").toStdString());
                        sourcePage_->setCurrentIndex(
                            original_.score.writtenMeasures[std::size_t(measure)].pageIndex);
                        sourceView_->viewport()->setCursor(Qt::CrossCursor);
                        error_->setText(trText("ui.original_playback.bind_help"));
                    }
                    catch (const std::exception &error)
                    {
                        bindingRow_ = -1;
                        error_->setText(QString::fromUtf8(error.what()));
                    }
                });
        sourceView_->emptyDoubleClicked = [this](QPointF center)
        {
            const int row = bindingRow_;
            if (row < 0 || row >= notes_->rowCount() || sourcePage_->currentIndex() < 0)
                return;
            const auto &image = original_.staffPages[std::size_t(sourcePage_->currentIndex())].sourceImage;
            if (!image.rect().contains(center.toPoint()))
                return;
            const double x = std::max(0.0, center.x() - 4);
            const double y = std::max(0.0, center.y() - 4);
            notes_->item(row, 9)->setCheckState(Qt::Checked);
            notes_->item(row, 9)->setData(Qt::UserRole, sourcePage_->currentIndex());
            notes_->item(row, 10)->setText(QString::number(x, 'g', 12));
            notes_->item(row, 11)->setText(QString::number(y, 'g', 12));
            notes_->item(row, 12)->setText(QString::number(std::min(8.0, image.width() - x), 'g', 12));
            notes_->item(row, 13)->setText(QString::number(std::min(8.0, image.height() - y), 'g', 12));
            bindingRow_ = -1;
            sourceView_->viewport()->unsetCursor();
            error_->clear();
        };
    }
    actions->addStretch();
    layout->addLayout(actions);
    error_ = new QLabel(this);
    error_->setObjectName("staffCorrectionError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    layout->addWidget(error_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("staffCorrectionButtons");
    buttons->button(QDialogButtonBox::Ok)->setObjectName("staffCorrectionApply");
    buttons->button(QDialogButtonBox::Ok)->setText(trText("ui.staff_correction.apply"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("staffCorrectionCancel");
    buttons->button(QDialogButtonBox::Cancel)->setText(trText("ui.button.cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    if (original_.staffPerformance)
        for (std::size_t i = 0; i < original_.staffPerformance->notes.size(); ++i)
            addNoteRow(&original_.staffPerformance->notes[i], int(i));
    showSourcePage(0);
}

void StaffCorrectionDialog::addNoteRow(const StaffPerformanceNote *note, int originalIndex)
{
    if (notes_->rowCount() >= 100000)
        return;
    const int row = notes_->rowCount();
    notes_->insertRow(row);
    for (int column = 0; column < 6; ++column)
        notes_->setItem(row, column, textItem({}));
    notes_->item(row, 0)->setData(Qt::UserRole, originalIndex);
    for (int column = 6; column < 9; ++column)
        notes_->setItem(row, column, checkItem(false));
    if (original_.staffImagePlayback)
    {
        notes_->setItem(row, 9, checkItem(note && note->hasImageAnchor));
        notes_->item(row, 9)->setData(Qt::UserRole, note ? note->pageIndex : -1);
        const auto box = note && note->hasImageAnchor ? note->source : SourceRect{};
        for (int column = 10; column < 14; ++column)
        {
            const double values[]{box.x, box.y, box.width, box.height};
            notes_->setItem(row, column, textItem(QString::number(values[column - 10], 'g', 12)));
        }
    }
    if (!note)
    {
        notes_->setCurrentCell(row, 0);
        return;
    }
    const auto &measures = original_.score.writtenMeasures;
    const auto after = std::upper_bound(measures.begin(), measures.end(), note->startTick,
                                        [](std::int64_t tick, const WrittenMeasure &measure)
                                        { return tick < measure.startTick; });
    const auto measure =
        after == measures.begin() ? std::size_t{0} : std::size_t(std::distance(measures.begin(), after) - 1);
    notes_->item(row, 0)->setText(QString::number(note->midiPitch));
    notes_->item(row, 1)->setText(QString::number(measure + 1));
    notes_->item(row, 2)->setText(beatText(note->startTick - measures[measure].startTick));
    notes_->item(row, 3)->setText(beatText(note->durationTicks));
    notes_->item(row, 4)->setText(QString::number(note->staff));
    notes_->item(row, 5)->setText(QString::fromStdString(note->voice));
    notes_->item(row, 6)->setCheckState(note->tieStart ? Qt::Checked : Qt::Unchecked);
    notes_->item(row, 7)->setCheckState(note->tieStop ? Qt::Checked : Qt::Unchecked);
    notes_->item(row, 8)->setCheckState(note->unresolvedSoundTie ? Qt::Checked : Qt::Unchecked);
}

void StaffCorrectionDialog::showSourcePage(int page)
{
    if (page < 0 || std::size_t(page) >= original_.staffPages.size())
    {
        sourceView_->setScore({}, Score{});
        return;
    }
    sourceView_->setScore(original_.staffPages[std::size_t(page)].sourceImage, Score{}, page);
    sourceView_->fitWidth();
}

void StaffCorrectionDialog::accept()
{
    try
    {
        auto measures = original_.score.writtenMeasures;
        std::int64_t tick = 0;
        for (int row = 0; row < measures_->rowCount(); ++row)
        {
            auto &measure = measures[std::size_t(row)];
            const int printedNumber = integerCell(*measures_, row, 1);
            if (printedNumber < 0 || printedNumber > 1000001)
                throw std::runtime_error(trText("ui.staff_correction.invalid_measure").toStdString());
            measure.number = printedNumber - 1;
            measure.beatsPerBar = integerCell(*measures_, row, 2);
            measure.beatUnit = integerCell(*measures_, row, 3);
            measure.durationTicks = beatCell(*measures_, row, 4);
            measure.startTick = tick;
            tick += measure.durationTicks;
        }
        std::vector<StaffPerformanceNote> notes;
        notes.reserve(std::size_t(notes_->rowCount()));
        for (int row = 0; row < notes_->rowCount(); ++row)
        {
            const int sequence = integerCell(*notes_, row, 1);
            if (sequence < 1 || sequence > int(measures.size()))
                throw std::runtime_error(trText("ui.staff_correction.invalid_position").toStdString());
            const int index = sequence - 1;
            StaffPerformanceNote note;
            const int originalIndex = notes_->item(row, 0)->data(Qt::UserRole).toInt();
            if (originalIndex >= 0 && original_.staffPerformance &&
                originalIndex < int(original_.staffPerformance->notes.size()))
                note = original_.staffPerformance->notes[std::size_t(originalIndex)];
            note.midiPitch = integerCell(*notes_, row, 0);
            const auto onset = beatCell(*notes_, row, 2);
            note.durationTicks = beatCell(*notes_, row, 3);
            if (onset >= measures[std::size_t(index)].durationTicks || note.durationTicks <= 0 ||
                note.durationTicks > measures[std::size_t(index)].durationTicks - onset)
                throw std::runtime_error(trText("ui.staff_correction.bar_bound").toStdString());
            note.startTick = measures[std::size_t(index)].startTick + onset;
            note.staff = integerCell(*notes_, row, 4);
            note.voice = requiredText(*notes_, row, 5).toStdString();
            note.tieStart = notes_->item(row, 6)->checkState() == Qt::Checked;
            note.tieStop = notes_->item(row, 7)->checkState() == Qt::Checked;
            note.unresolvedSoundTie = notes_->item(row, 8)->checkState() == Qt::Checked;
            if (original_.staffImagePlayback)
            {
                note.hasImageAnchor = notes_->item(row, 9)->checkState() == Qt::Checked;
                note.source = {};
                if (note.hasImageAnchor)
                {
                    const int anchoredPage = notes_->item(row, 9)->data(Qt::UserRole).toInt();
                    if (anchoredPage >= 0)
                        note.pageIndex = anchoredPage;
                    double values[4]{};
                    for (int column = 10; column < 14; ++column)
                    {
                        bool valid = false;
                        values[column - 10] = requiredText(*notes_, row, column).toDouble(&valid);
                        if (!valid || !std::isfinite(values[column - 10]))
                            throw std::runtime_error(trText("messages.pages.invalid_project").toStdString());
                    }
                    note.source = {values[0], values[1], values[2], values[3]};
                }
            }
            notes.push_back(std::move(note));
        }
        corrected_ = correctedStaffProject(original_, std::move(notes), std::move(measures));
        QDialog::accept();
    }
    catch (const std::exception &error)
    {
        corrected_.reset();
        error_->setText(QString::fromUtf8(error.what()));
    }
}

std::optional<Project> StaffCorrectionDialog::correctedProject() const
{
    return corrected_;
}
} // namespace singlilt
