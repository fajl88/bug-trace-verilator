// -*- mode: C++; c-file-style: "cc-mode" -*-
//=============================================================================
//
// Code available from: https://verilator.org
//
// SPDX-FileCopyrightText: 2001-2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//=============================================================================
///
/// \file
/// \brief VerilatedTraceBaseC — minimal header for generated causality hooks
///
/// VerilatedContext stores VerilatedTraceBaseC* and generated code calls
/// causalityEmitFromEval on it; the complete class must be visible wherever
/// verilated.h is included. Kept separate from verilated_trace.h to avoid
/// pulling the full trace implementation into every compilation unit.
///
//=============================================================================

#ifndef VERILATOR_VERILATED_TRACE_BASE_C_H_
#define VERILATOR_VERILATED_TRACE_BASE_C_H_

#ifndef VERILATOR_VERILATED_H_
# error "verilated_trace_base_c.h must be included after verilated.h"
#endif

//=============================================================================
// VerilatedTraceBaseC - base class of all Verilated*C trace classes
// Internal use only

class VerilatedTraceBaseC VL_NOT_FINAL {
    bool m_modelConnected = false;  // Model connected by calling Verilated::trace()
public:
    /// True if file currently open
    virtual bool isOpen() const VL_MT_SAFE = 0;

    /// Emit causality event outside trace callbacks (e.g. assignment evaluation site).
    virtual void causalityEmitFromEval(uint64_t timeui, uint32_t sinkCode, uint32_t writeSiteId,
                                       const uint32_t* predCodes, const uint8_t* predRoles,
                                       const int8_t* predTimeDeltas, uint32_t predCount,
                                       bool valueChanged) VL_MT_SAFE {
        // Default no-op when causality is not wired on this trace implementation.
        (void)timeui;
        (void)sinkCode;
        (void)writeSiteId;
        (void)predCodes;
        (void)predRoles;
        (void)predTimeDeltas;
        (void)predCount;
        (void)valueChanged;
    }

    // internal use only
    bool modelConnected() const VL_MT_SAFE { return m_modelConnected; }
    void modelConnected(bool flag) VL_MT_SAFE { m_modelConnected = flag; }
};

#endif  // VERILATOR_VERILATED_TRACE_BASE_C_H_
