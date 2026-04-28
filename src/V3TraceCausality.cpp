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

    void applyPreds(const std::vector<AstVarScope*>& sinkps, const std::vector<AstVarScope*>& predps,
                    bool includeSelf) {
        for (AstVarScope* const sinkp : sinkps) {
            if (!sinkp) continue;
            for (AstVarScope* const predp : predps) {
                if (predp != sinkp) sinkp->causalityPredVscpAdd(predp);
            }
            for (AstVarScope* const predp : m_ctrlPreds) {
                if (predp != sinkp) sinkp->causalityPredVscpAdd(predp);
            }
            for (AstVarScope* const predp : m_activePreds) {
                if (predp != sinkp) sinkp->causalityPredVscpAdd(predp);
            }
            if (includeSelf) sinkp->causalityPredVscpAdd(sinkp);
        }
    }

    void visit(AstNetlist* nodep) override {
        nodep->foreach([](AstVarScope* vscp) { vscp->causalityPredVscpsClear(); });
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

    void visit(AstNodeAssign* nodep) override {
        const std::vector<AstVarScope*> sinkps = collectWrittenSinks(nodep->lhsp());
        std::vector<AstVarScope*> predps = collectReadPreds(nodep->rhsp());
        if (nodep->timingControlp()) {
            for (AstVarScope* const predp : collectReadPreds(nodep->timingControlp())) {
                appendUnique(predps, predp);
            }
        }
        applyPreds(sinkps, predps, VN_IS(nodep, AssignDly));
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
