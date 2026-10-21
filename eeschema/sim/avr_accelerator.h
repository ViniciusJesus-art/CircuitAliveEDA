/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
struct JSContext;
namespace CIRCUITALIVE
{
// Pure, bounded CPU execution. No OS access, peripheral callbacks or imports.
void InstallAvrAccelerator( JSContext* aContext, int* aExecutionBudget );
}
