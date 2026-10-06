// Staff-page resource and time-range serialization.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "ProjectStore.h"
#include <QJsonArray>
namespace singlilt
{
QString staffSourceRole(int pageIndex);
QString staffRenderedRole(int pageIndex);
QJsonArray staffPagesToJson(const Project &project);
void validateStaffPages(const Project &project);
void validateStaffPageMetadata(const QJsonArray &metadata, const std::vector<StaffPage> &pages);
} // namespace singlilt
