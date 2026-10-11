// Application and current-score options with validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "settings/AppSettings.h"
#include <QDialog>
#include <functional>

class QTabWidget;
class QFormLayout;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
namespace singlilt
{
class OptionsDialog final : public QDialog
{
  public:
    OptionsDialog(const AppSettings &settings, const OptionsContext &context, QWidget *parent = nullptr);
    std::function<bool(const AppSettings &, const OptionsContext &)> applyChanges;
    void selectPage(int index);

  protected:
    void changeEvent(QEvent *event) override;

  private:
    void retranslate();
    void refreshCapabilities();
    void showError(const QString &message, const char *field = nullptr);
    bool commit();
    QLineEdit *pathRow(QFormLayout *form, const char *key, const char *name, const QString &path, bool directory);
    AppSettings initial_;
    OptionsContext context_;
    QTabWidget *tabs_;
    QLabel *error_;
    QFormLayout *errorForm_ = nullptr;
    QLabel *pianoStatus_, *gmStatus_, *whisperStatus_, *separationStatus_;
    QComboBox *language_, *theme_, *backend_, *devices_, *difficulty_, *guide_, *lyricLanguage_, *source_,
        *pattern_, *programA_, *programB_, *currentProgramA_, *currentProgramB_, *tonic_;
    QLineEdit *directory_, *gm_, *endpoint_, *model_, *key_, *whisper_;
    QCheckBox *startup_, *accent_, *metronome_, *separate_, *recognize_, *currentAccent_, *melody_,
        *accompaniment_;
    QCheckBox *markers_, *enhance_, *outputBoost_;
    QDoubleSpinBox *melodyVolume_, *accompanimentVolume_, *originalVolume_, *originalSpeed_, *tolerance_,
        *practiceSpeed_, *tempo_, *speed_, *currentMelodyVolume_, *currentAccompanimentVolume_,
        *currentOriginalSpeed_, *currentOriginalVolume_;
    QSpinBox *velocity_, *latency_, *timeout_, *meterTop_, *meterBottom_, *transpose_, *currentVelocity_;
};
} // namespace singlilt
