// Recorded pitch and target-note feedback display.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/SingingAssessment.h"
#include <QWidget>

namespace singlilt
{
class PitchCurve final : public QWidget
{
  public:
    explicit PitchCurve(QWidget *parent = nullptr);
    void setFrames(std::vector<ExpectedTone> targets, std::vector<PitchObservation> frames, bool reveal);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    std::vector<ExpectedTone> targets_;
    std::vector<PitchObservation> frames_;
    bool reveal_ = true;
};
} // namespace singlilt
