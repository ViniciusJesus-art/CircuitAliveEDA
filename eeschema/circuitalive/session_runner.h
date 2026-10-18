/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "circuit_snapshot.h"
#include "scope_history.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace CIRCUITALIVE
{
/// Installed bin/, standalone source build, and source-build KiCad project manager layouts.
std::string FindSessionWorker( const std::string& aApplicationExecutable );

enum class EDITOR_SESSION_STATE { STOPPED, PREPARING, RUNNING, PAUSED, COMPLETED, ERROR };

struct EDITOR_SESSION_VIEW
{
    EDITOR_SESSION_STATE state = EDITOR_SESSION_STATE::STOPPED;
    SIM_SESSION_SNAPSHOT sample;
    std::vector<bool> switches;
    std::vector<double> adjustments;
    std::vector<double> ledCurrents;
    std::vector<double> measurements;
    std::shared_ptr<const SCOPE_HISTORY> scopeHistory;
    std::vector<std::string> warnings;
    std::vector<LOGIC_LEVEL> logicLevels;
    std::vector<bool> logicRapid;
    std::string error;
    std::shared_ptr<const MEMORY_SNAPSHOT> memory;
    std::uint64_t memorySequence = 0;
    std::string memoryError;
    double speed = 1;
    SIMULATION_PREFERENCES preferences;
};

/// One owner thread for all blocking engine operations. GUI polls copied, bounded state.
class SESSION_RUNNER
{
public:
    explicit SESSION_RUNNER( std::string aWorker );
    ~SESSION_RUNNER();
    void Start( CIRCUIT_SNAPSHOT aCircuit );
    void Resume();
    void Pause();
    void Stop();
    void ToggleSwitch( std::size_t aIndex );
    void SetAdjustment( std::size_t aIndex, double aValue );
    void SetSpeed( double aSpeed );
    void SetPreferences( SIMULATION_PREFERENCES aPreferences );
    void Step();
    void CaptureMemory( std::size_t aRegion );
    EDITOR_SESSION_VIEW View() const;

private:
    enum class COMMAND_TYPE { START, RESUME, PAUSE, STOP, TOGGLE, ADJUST, SPEED, STEP, MEMORY, PREFERENCES };
    struct COMMAND
    {
        COMMAND_TYPE type;
        std::uint64_t revision;
        CIRCUIT_SNAPSHOT circuit;
        std::size_t index = 0;
        double value = 0;
        SIMULATION_PREFERENCES preferences = {};
    };
    void enqueue( COMMAND_TYPE aType, std::size_t aIndex = 0 );
    void run();
    std::string m_worker;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<COMMAND> m_commands;
    EDITOR_SESSION_VIEW m_view;
    std::uint64_t m_revision = 0;
    bool m_quit = false;
    double m_speed = 1;
    SIMULATION_PREFERENCES m_preferences;
    std::thread m_thread;
};
}
