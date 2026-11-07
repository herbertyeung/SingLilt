// Measure controls commit through the authored-score transaction.
#include "MainWindow.h"
#include "i18n/LanguageManager.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <algorithm>
#include <QSpinBox>
#include <QPushButton>
#include <limits>
#include <stdexcept>

namespace singlilt
{
namespace
{
class RepeatRange final : public QListWidget
{
  public:
    using QListWidget::QListWidget;

  protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const auto anchor = currentIndex();
        const auto target = indexAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier) &&
            anchor.isValid() && target.isValid())
        {
            const auto first = model()->index(std::min(anchor.row(), target.row()), 0);
            const auto last = model()->index(std::max(anchor.row(), target.row()), 0);
            selectionModel()->select(QItemSelection(first, last), QItemSelectionModel::ClearAndSelect);
            event->accept();
            return;
        }
        QListWidget::mousePressEvent(event);
    }
};
} // namespace

void MainWindow::editNumberedStructure(bool repeat)
{
    try
    {
        auto composition = currentNumberedComposition();
        const auto measures = numberedMeasures(project_.score, composition);
        const int count = int(measures.size());
        const auto current = selected_ >= 0 ? project_.score.notes[std::size_t(selected_)].measure
                                            : int(numberedMeasureAt(measures, numberedInputTick_));
        QDialog dialog(this);
        dialog.setWindowTitle(trText(repeat ? "ui.editor.measure_repeat" : "ui.editor.measure_properties"));
        QFormLayout form(&dialog);
        const auto spin = [&](const char *key, const char *name, int minimum, int maximum, int value)
        {
            auto *field = new QSpinBox;
            field->setObjectName(name);
            field->setRange(minimum, maximum);
            field->setValue(value);
            form.addRow(trText(key), field);
            return field;
        };
        QSpinBox *first = nullptr, *last = nullptr, *ending = nullptr, *passes = nullptr;
        QSpinBox *measure = nullptr, *beats = nullptr;
        QComboBox *unit = nullptr, *tonic = nullptr;
        QDoubleSpinBox *tempo = nullptr;
        constexpr int RemoveRepeat = 2;
        QPushButton *remove = nullptr;
        int editedRepeatFirstId = 0;
        if (repeat)
        {
            const auto measureNumber = [&measures](int id)
            {
                if (!id)
                    return 0;
                const auto span = std::find_if(measures.begin(), measures.end(),
                                               [id](const auto &measure) { return measure.id == id; });
                return span == measures.end() ? 0 : int(span - measures.begin()) + 1;
            };
            const NumberedMeasureRepeat *existing = nullptr;
            for (const auto &entry : composition.structure.repeats)
                if (measureNumber(entry.first) <= current + 1 && current + 1 <= measureNumber(entry.last))
                {
                    existing = &entry;
                    editedRepeatFirstId = entry.first;
                    break;
                }
            const int initialFirst = existing ? measureNumber(existing->first) : current + 1;
            dialog.setWindowTitle(trText(existing ? "ui.editor.repeat_edit" : "ui.editor.repeat_new"));
            auto *hint = new QLabel(trText("ui.editor.repeat_range_hint"), &dialog);
            hint->setWordWrap(true);
            form.addRow(hint);
            auto *range = new RepeatRange(&dialog);
            range->setObjectName("repeatRange");
            range->setAccessibleName(trText("ui.editor.repeat_range"));
            range->setFlow(QListView::LeftToRight);
            range->setSelectionMode(QAbstractItemView::ContiguousSelection);
            range->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
            range->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            range->setFixedHeight(62);
            for (int bar = 1; bar <= count; ++bar)
            {
                auto *item = new QListWidgetItem(QString::number(bar), range);
                item->setTextAlignment(Qt::AlignCenter);
                item->setSizeHint(QSize(38, 32));
            }
            form.addRow(range);
            dialog.setMinimumWidth(380);
            first = spin("ui.editor.structure_first", "repeatFirst", 1, count, initialFirst);
            last = spin("ui.editor.structure_last", "repeatLast", initialFirst, count,
                        existing ? measureNumber(existing->last) : initialFirst);
            ending = spin("ui.editor.structure_ending", "repeatEnding", 0, count,
                          existing ? measureNumber(existing->firstEnding) : 0);
            passes = spin("ui.editor.structure_passes", "repeatPasses", 2, 8, existing ? existing->count : 2);
            remove = new QPushButton(trText("ui.editor.repeat_remove"), &dialog);
            remove->setObjectName("repeatRemove");
            remove->setEnabled(existing != nullptr);
            const auto selectRange = [range, first, last]
            {
                const QSignalBlocker blocker(range->selectionModel());
                const auto start = range->model()->index(first->value() - 1, 0);
                const auto end = range->model()->index(last->value() - 1, 0);
                range->selectionModel()->setCurrentIndex(start, QItemSelectionModel::NoUpdate);
                range->selectionModel()->select(QItemSelection(start, end), QItemSelectionModel::ClearAndSelect);
                range->scrollTo(start);
            };
            connect(first, &QSpinBox::valueChanged, &dialog,
                    [last, selectRange](int value)
                    {
                        const QSignalBlocker blockLast(last);
                        last->setMinimum(value);
                        selectRange();
                    });
            connect(last, &QSpinBox::valueChanged, &dialog, selectRange);
            connect(range, &QListWidget::itemSelectionChanged, &dialog,
                    [range, first, last]
                    {
                        const auto rows = range->selectionModel()->selectedIndexes();
                        if (rows.isEmpty())
                            return;
                        const auto [begin, end] =
                            std::minmax_element(rows.begin(), rows.end(),
                                                [](const auto &a, const auto &b) { return a.row() < b.row(); });
                        const QSignalBlocker blockFirst(first);
                        const QSignalBlocker blockLast(last);
                        first->setValue(begin->row() + 1);
                        last->setMinimum(first->value());
                        last->setValue(end->row() + 1);
                    });
            connect(remove, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(RemoveRepeat); });
            selectRange();
        }
        else
        {
            measure = spin("ui.editor.structure_measure", "structureMeasure", 1, count, current + 1);
            beats = spin("ui.editor.structure_beats", "structureBeats", 0, MaximumBeatsPerBar, 0);
            beats->setSpecialValueText(trText("ui.editor.structure_inherit"));
            unit = new QComboBox;
            unit->setObjectName("structureUnit");
            for (int value = 1; value <= MaximumBeatUnit; value *= 2)
                unit->addItem(QString::number(value), value);
            form.addRow(trText("ui.editor.structure_unit"), unit);
            tempo = new QDoubleSpinBox;
            tempo->setObjectName("structureTempo");
            tempo->setRange(0, MaximumScoreBpm);
            tempo->setSpecialValueText(trText("ui.editor.structure_inherit"));
            tempo->setDecimals(2);
            form.addRow(trText("ui.editor.structure_tempo"), tempo);
            tonic = new QComboBox;
            tonic->setObjectName("structureTonic");
            tonic->addItem(trText("ui.editor.structure_inherit"), -1);
            for (int key = 0; key < 12; ++key)
                tonic->addItem(trText(qPrintable(QString("ui.key.%1").arg(key))), key);
            form.addRow(trText("ui.editor.structure_tonic"), tonic);
            connect(beats, &QSpinBox::valueChanged, unit, [unit](int value) { unit->setEnabled(value != 0); });
            const auto loadMeasure = [&]
            {
                const auto i = std::size_t(measure->value() - 1);
                const auto &selected = measures[i];
                const auto spec = i < composition.structure.measures.size() ? composition.structure.measures[i]
                                                                            : NumberedMeasure{};
                beats->setValue(spec.beats);
                unit->setCurrentIndex(unit->findData(spec.unit ? spec.unit : selected.unit));
                tempo->setValue(spec.bpm);
                tonic->setCurrentIndex(tonic->findData(spec.tonic));
                unit->setEnabled(spec.beats != 0);
            };
            connect(measure, &QSpinBox::valueChanged, &dialog, loadMeasure);
            loadMeasure();
        }
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        if (remove)
            buttons.addButton(remove, QDialogButtonBox::DestructiveRole);
        form.addRow(&buttons);
        bindText(buttons.button(QDialogButtonBox::Ok), "ui.inspector.apply");
        bindText(buttons.button(QDialogButtonBox::Cancel), "ui.button.cancel");
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        const int result = dialog.exec();
        if (result != QDialog::Accepted && result != RemoveRepeat)
            return;
        auto &structure = composition.structure;
        while (structure.measures.size() < measures.size())
        {
            if (structure.nextId == std::numeric_limits<int>::max())
                throw std::invalid_argument("ui.editor.invalid_structure");
            structure.measures.push_back({structure.nextId++});
        }
        if (repeat)
        {
            const auto id = [&](int value) { return value ? structure.measures[std::size_t(value - 1)].id : 0; };
            const int start = id(first->value());
            if (editedRepeatFirstId)
                std::erase_if(structure.repeats, [editedRepeatFirstId](const auto &entry)
                              { return entry.first == editedRepeatFirstId; });
            if (result != RemoveRepeat)
                structure.repeats.push_back({start, id(last->value()), id(ending->value()), passes->value()});
        }
        else
        {
            auto &spec = structure.measures[std::size_t(measure->value() - 1)];
            spec.beats = beats->value();
            spec.unit = spec.beats ? unit->currentData().toInt() : 0;
            spec.bpm = tempo->value();
            spec.tonic = tonic->currentData().toInt();
        }
        commitNumberedComposition(project_.score, std::move(composition), activeNumberedVoice_, selected_,
                                  repeat ? "ui.editor.measure_repeat" : "ui.editor.measure_properties");
    }
    catch (const std::exception &error)
    {
        setStatusMessage(QString::fromUtf8(error.what()));
    }
}
} // namespace singlilt
