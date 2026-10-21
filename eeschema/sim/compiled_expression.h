/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace CIRCUITALIVE
{
/** Bounded evaluation and automatic derivatives of supported SPICE B expressions.
 * Unsupported or potentially singular expressions remain ngspice sources. No lookup tables,
 * compiler process, generated machine code or platform dependency is involved.
 */
class COMPILED_EXPRESSIONS
{
public:
    COMPILED_EXPRESSIONS();
    ~COMPILED_EXPRESSIONS();
    COMPILED_EXPRESSIONS( COMPILED_EXPRESSIONS&& ) noexcept;
    COMPILED_EXPRESSIONS& operator=( COMPILED_EXPRESSIONS&& ) noexcept;
    std::string Configure( const std::string& aDeck, const std::vector<std::string>& aSignals,
                           const std::vector<std::string>& aMutableElements = {} );
    bool Evaluate( unsigned aSlot, const double* aInputs, unsigned aCount, double aTime,
                   double& aValue, const double*& aPartials );
    bool Empty() const;
    std::string Error() const;
private:
    struct IMPL;
    std::unique_ptr<IMPL> m_impl;
};
}
