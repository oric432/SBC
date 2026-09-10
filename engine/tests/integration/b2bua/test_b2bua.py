from __future__ import annotations

import logging

import pytest

from .scenarios import SCENARIOS

_log = logging.getLogger("integration.b2bua")


@pytest.mark.parametrize("scenario", SCENARIOS, ids=lambda s: s.name)
def test_b2bua_call(sbc_engine, render_scenario, run_sipp_pair, scenario):
    total_hold_s = sum(scenario.holds_ms) / 1000
    _log.info("running scenario '%s' (~%.0fs call)...", scenario.name, total_hold_s)

    caller_xml = render_scenario("caller.xml.j2", scenario_name=scenario.name, holds_ms=scenario.holds_ms)
    callee_xml = render_scenario("callee.xml.j2", scenario_name=scenario.name)

    run_sipp_pair(caller_xml, callee_xml)

    assert not sbc_engine.has_error_logs(), f"engine logged an error during the call:\n{sbc_engine.log_tail()}"
    _log.info("scenario '%s' passed", scenario.name)
