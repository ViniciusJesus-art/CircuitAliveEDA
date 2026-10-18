/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_design_presets.h"
namespace CIRCUITALIVE
{
MODEL_DESIGN MakeTimer555Design( const std::map<std::string, std::string>& t )
{
    MODEL_DESIGN d = MODEL_DESIGN::Open( t.at( "ne555" ) );
    d.baseJson.clear(); d.blocks.clear(); d.id = "custom_555"; d.name = "Custom 555 timer"; d.libraryId = "Timer:NE555P";
    d.description = "555 assembled visually from divider, comparators, reset-priority latch, output and discharge blocks.";
    auto add = [&]( const std::string& id, const std::string& type, std::map<std::string, std::string> pins,
                    std::map<std::string, std::string> values, int x, int y )
    { d.blocks.push_back( { id, t.at( type ), std::move( pins ), std::move( values ), x, y } ); };
    add( "Rtop", "resistor", { { "p", "@vcc" }, { "n", "@ctrl" } }, { { "resistance", "@divider" } }, 280, 65 );
    add( "Rmid", "resistor", { { "p", "@ctrl" }, { "n", "half" } }, { { "resistance", "@divider" } }, 280, 245 );
    add( "Rbot", "resistor", { { "p", "half" }, { "n", "@gnd" } }, { { "resistance", "@divider" } }, 280, 425 );
    const std::map<std::string, std::string> comparatorValues{ { "vmin", "@vmin" }, { "vmax", "@vmax" } };
    add( "Trigger", "comparator", { { "plus", "half" }, { "minus", "@trig" }, { "out", "set" }, { "vcc", "@vcc" }, { "gnd", "@gnd" } }, comparatorValues, 600, 65 );
    add( "Threshold", "comparator", { { "plus", "@thresh" }, { "minus", "@ctrl" }, { "out", "threshold" }, { "vcc", "@vcc" }, { "gnd", "@gnd" } }, comparatorValues, 600, 340 );
    add( "ResetRef", "dc_voltage", { { "p", "reset_ref" }, { "n", "@gnd" } }, { { "voltage", "@reset_threshold" } }, 280, 640 );
    add( "Reset", "comparator", { { "plus", "reset_ref" }, { "minus", "@reset" }, { "out", "clear" }, { "vcc", "@vcc" }, { "gnd", "@gnd" } }, comparatorValues, 600, 615 );
    add( "Latch", "sr_reset_priority", { { "s", "set" }, { "r", "threshold" }, { "reset", "clear" }, { "q", "state" }, { "vcc", "@vcc" }, { "gnd", "@gnd" } },
        { { "delay", "@delay" }, { "transition", "@transition" }, { "vmin", "@vmin" }, { "vmax", "@vmax" } }, 920, 230 );
    add( "Output", "pushpull", { { "state", "state" }, { "out", "@out" }, { "vcc", "@vcc" }, { "gnd", "@gnd" } },
        { { "rout", "@rout" }, { "imax", "@imax" }, { "high_drop", "@high_drop" }, { "low_drop", "@low_drop" }, { "cout", "@cout" }, { "vmin", "@vmin" }, { "vmax", "@vmax" } }, 1240, 110 );
    add( "Discharge", "discharge", { { "state", "state" }, { "out", "@discharge" }, { "vcc", "@vcc" }, { "gnd", "@gnd" } },
        { { "ron", "@discharge_r" }, { "vmin", "@vmin" }, { "vmax", "@vmax" } }, 1240, 420 );
    add( "Consumption", "constant_current", { { "p", "@vcc" }, { "n", "@gnd" } }, { { "current", "@iq" } }, 920, 640 );
    return d;
}
}
