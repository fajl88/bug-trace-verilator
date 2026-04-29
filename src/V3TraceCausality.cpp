// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Static causality fan-in extraction
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3TraceCausality.h"

#include "V3Global.h"

VL_DEFINE_DEBUG_FUNCTIONS;

enum class CausalityRole : uint8_t {
    DATA = 1,
    CONTROL_GUARD = 2,
    SEQUENTIAL_TRIGGER = 3,
    PRIOR_STATE = 4,
    RESET_CAUSE = 5,
    ENABLE_GUARD = 6,
};

class TraceCausalityVisitor final : public VNVisitor {
    std::vector<AstVarScope*> m_activePreds;
    std::vector<AstVarScope*> m_ctrlPreds;

    static void appendUnique(std::vector<AstVarScope*>& vec, AstVarScope* vscp) {
        if (!vscp) return;
        for (AstVarScope* const existingp : vec) {
            if (existingp == vscp) return;
        }
        vec.push_back(vscp);
    }

    static std::vector<AstVarScope*> collectReadPreds(AstNode* nodep) {
        std::vector<AstVarScope*> predps;
        if (!nodep) return predps;
        nodep->foreach([&predps](AstVarRef* refp) {
            if (refp->access().isWriteOnly()) return;
            appendUnique(predps, refp->varScopep());
        });
        // Function/task calls: prefer internal provenance when available, otherwise
        // conservatively attribute argument-side reads as boundary causes.
        nodep->foreach([&predps](AstNodeFTaskRef* callp) {
            if (!callp) return;
            if (AstNodeFTask* const taskp = callp->taskp()) {
                taskp->foreach([&predps](AstVarRef* refp) {
                    if (refp->access().isWriteOnly()) return;
                    appendUnique(predps, refp->varScopep());
                });
            } else {
                for (AstNode* argnodep = callp->argsp(); argnodep; argnodep = argnodep->nextp()) {
                    AstArg* const argp = VN_CAST(argnodep, Arg);
                    if (!argp) continue;
                    for (AstVarScope* const predp : collectReadPreds(argp->exprp())) {
                        appendUnique(predps, predp);
                    }
                }
            }
        });
        return predps;
    }
    static std::vector<AstVarScope*> collectCondPreds(AstNode* nodep) {
        std::vector<AstVarScope*> predps;
        if (!nodep) return predps;
        nodep->foreach([&predps](AstCond* condp) {
            if (!condp->condp()) return;
            for (AstVarScope* const predp : collectReadPreds(condp->condp())) appendUnique(predps, predp);
        });
        return predps;
    }

    static std::vector<AstVarScope*> collectWrittenSinks(AstNode* nodep) {
        std::vector<AstVarScope*> sinkps;
        if (!nodep) return sinkps;
        nodep->foreach([&sinkps](AstVarRef* refp) {
            if (!refp->access().isWriteOrRW()) return;
            appendUnique(sinkps, refp->varScopep());
        });
        return sinkps;
    }

    void addSinkRolePreds(const std::vector<AstVarScope*>& sinkps, const std::vector<AstVarScope*>& predps,
                         CausalityRole role) {
        for (AstVarScope* const sinkp : sinkps) {
            if (!sinkp) continue;
            for (AstVarScope* const predp : predps) {
                if (predp != sinkp) sinkp->causalityPredEdgeAdd(predp, static_cast<uint8_t>(role));
            }
        }
    }

    void visit(AstNetlist* nodep) override {
        nodep->foreach([](AstVarScope* vscp) { vscp->causalityPredEdgesClear(); });
        iterateChildren(nodep);
    }

    void visit(AstActive* nodep) override {
        const std::vector<AstVarScope*> savedPreds = m_activePreds;
        m_activePreds.clear();
        if (AstSenTree* const sentreep = nodep->sentreep()) {
            sentreep->foreach([this](AstVarRef* refp) {
                if (refp->access().isWriteOnly()) return;
                appendUnique(m_activePreds, refp->varScopep());
            });
        }
        iterateChildren(nodep);
        m_activePreds = savedPreds;
    }

    void visit(AstIf* nodep) override {
        const size_t oldSize = m_ctrlPreds.size();
        for (AstVarScope* const predp : collectReadPreds(nodep->condp())) appendUnique(m_ctrlPreds, predp);
        iterateChildren(nodep);
        m_ctrlPreds.resize(oldSize);
    }

    void visit(AstCase* nodep) override {
        const size_t oldSize = m_ctrlPreds.size();
        for (AstVarScope* const predp : collectReadPreds(nodep->exprp())) appendUnique(m_ctrlPreds, predp);
        for (AstCaseItem* itemp = nodep->itemsp(); itemp; itemp = VN_AS(itemp->nextp(), CaseItem)) {
            for (AstNode* condsp = itemp->condsp(); condsp; condsp = condsp->nextp()) {
                for (AstVarScope* const predp : collectReadPreds(condsp)) appendUnique(m_ctrlPreds, predp);
            }
        }
        iterateChildren(nodep);
        m_ctrlPreds.resize(oldSize);
    }

    void visit(AstNodeAssign* nodep) override {
        const std::vector<AstVarScope*> sinkps = collectWrittenSinks(nodep->lhsp());
        std::vector<AstVarScope*> predps = collectReadPreds(nodep->rhsp());
        const std::vector<AstVarScope*> condPreds = collectCondPreds(nodep->rhsp());
        if (nodep->timingControlp()) {
            for (AstVarScope* const predp : collectReadPreds(nodep->timingControlp())) {
                appendUnique(predps, predp);
            }
        }
        addSinkRolePreds(sinkps, predps, CausalityRole::DATA);
        addSinkRolePreds(sinkps, condPreds, CausalityRole::CONTROL_GUARD);
        addSinkRolePreds(sinkps, m_ctrlPreds, CausalityRole::CONTROL_GUARD);
        addSinkRolePreds(sinkps, m_activePreds, CausalityRole::SEQUENTIAL_TRIGGER);
        if (VN_IS(nodep, AssignDly)) {
            for (AstVarScope* const sinkp : sinkps) {
                if (!sinkp) continue;
                sinkp->causalityPredEdgeAdd(sinkp, static_cast<uint8_t>(CausalityRole::PRIOR_STATE));
            }
        }
        iterateChildren(nodep);
    }

    void visit(AstNode* nodep) override { iterateChildren(nodep); }

public:
    explicit TraceCausalityVisitor(AstNetlist* nodep) { iterate(nodep); }
};

void V3TraceCausality::causalityAll(AstNetlist* nodep) {
    UINFO(2, __FUNCTION__ << ":");
    { TraceCausalityVisitor{nodep}; }
    V3Global::dumpCheckGlobalTree("tracecausality", 0, dumpTreeEitherLevel() >= 3);
}
