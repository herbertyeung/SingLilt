// Arrangement review, chord editing, and preview controls.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/Accompaniment.h"
#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace singlilt
{
// Owns only a candidate snapshot. The main window owns confirmed project state.
class AccompanimentPanel final : public QDialog
{
  public:
    AccompanimentPanel(const Score &score, AccompanimentArrangement candidate, QWidget *parent = nullptr);
    AccompanimentPanel(const Score &score, AccompanimentArrangement candidate,
                       std::optional<AccompanimentArrangement> confirmed, QWidget *parent = nullptr);
    const AccompanimentArrangement &candidate() const;
    void setSourceFingerprint(const std::string &fingerprint);
    void retranslateUi();
    std::function<bool(const AccompanimentArrangement &)> auditionRequested;
    std::function<bool(const AccompanimentArrangement &)> confirmationRequested;
    std::function<void()> cancelled;

  private:
    void refreshTable();
    void refreshCandidates();
    void chooseWholeSong();
    void selectManualDraft(AccompanimentArrangement arrangement);
    void copyCandidate();
    void restoreConfirmed();
    void createBlankDraft();
    void saveManualDraft();
    void refreshAudition();
    void applyInterval();
    void splitInterval();
    void mergeInterval();
    void addInterval();
    void deleteInterval();
    bool applyManualArrangement(AccompanimentArrangement arrangement, int selectedRow);
    QString musicalInterval(const ChordSpan &span) const;
    void selectChord();
    void editChord();
    void selectAlternative();
    void regenerate();
    void updateValidity();
    Score score_;
    AccompanimentArrangement candidate_;
    std::vector<AccompanimentCandidate> candidates_;
    std::optional<AccompanimentArrangement> confirmedArrangement_, manualDraft_;
    std::vector<SourceMeasureRange> measures_;
    int selectedCandidate_ = 0;
    std::string currentFingerprint_;
    bool auditioning_ = false;
    bool stale_ = false;
    bool updating_ = false;
    bool confirmed_ = false;
    QTableWidget *chords_ = nullptr;
    QListWidget *wholeSong_ = nullptr, *sections_ = nullptr;
    QLineEdit *draftName_ = nullptr;
    QWidget *advanced_ = nullptr;
    QComboBox *pattern_ = nullptr, *mode_ = nullptr, *tonic_ = nullptr, *alternatives_ = nullptr, *root_ = nullptr,
              *quality_ = nullptr, *sectionPattern_ = nullptr;
    QSpinBox *inversion_ = nullptr, *startTick_ = nullptr, *endTick_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *audition_ = nullptr, *confirm_ = nullptr;
};
} // namespace singlilt
