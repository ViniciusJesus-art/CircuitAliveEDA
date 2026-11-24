/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace CIRCUITALIVE
{
// The desktop owns playback; solver/IPC threads never wait for audio hardware.
class SIMULATION_AUDIO
{
public:
    SIMULATION_AUDIO();
    ~SIMULATION_AUDIO();
    void Submit(const std::vector<std::int16_t>&,bool enabled);
    void Flush();
    std::string Error() const;
private:
    struct IMPL;std::unique_ptr<IMPL> m_impl;
};
}
