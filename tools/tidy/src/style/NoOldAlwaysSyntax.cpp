// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "ASTHelperVisitors.h"
#include "TidyDiags.h"

#include "slang/syntax/AllSyntax.h"

using namespace slang;
using namespace slang::ast;

namespace no_old_always_syntax {

/// Looks for Assignment and Variable Declarations to determine whether non local assignments are
/// made
struct AssignmentLookup : public ASTVisitor<AssignmentLookup, true, true> {
    void handle(ast::AssignmentExpression const& expr) {
        auto left_symbol = expr.left().getSymbolReference();
        assignments.emplace_back(&expr);
    }

    void handle(ast::VariableDeclStatement const& stmt) {
        localVariables.emplace_back(&stmt.symbol);
    }

    std::vector<AssignmentExpression const*> nonLocalAssignments() {
        auto is_not_local = [&](AssignmentExpression const* expr) {
            auto symbol = expr->left().getSymbolReference();
            return std::find(localVariables.begin(), localVariables.end(), symbol) ==
                   localVariables.end();
        };

        std::vector<AssignmentExpression const*> result;
        std::copy_if(assignments.begin(), assignments.end(), std::back_inserter(result),
                     is_not_local);

        return result;
    }

    std::vector<VariableSymbol const*> localVariables;
    std::vector<AssignmentExpression const*> assignments;
};

struct MainVisitor : public TidyVisitor, ASTVisitor<MainVisitor, true, true> {
    explicit MainVisitor(Diagnostics& diagnostics) : TidyVisitor(diagnostics) {}

    void handle(const ast::ProceduralBlockSymbol& symbol) {
        NEEDS_SKIP_SYMBOL(symbol)

        if (symbol.isFromAssertion)
            return;

        if (symbol.procedureKind == ProceduralBlockKind::Always) {
            // There are still legit uses of always_comb (e.g. for formal verification). To prevent
            // warnings with such always blocks, it is checked whether the block contains
            // assignments to variables of an upper scope. Such assignments suggest that
            // always_{comb,latch,ff} blocks are better suited here.
            AssignmentLookup lookup;
            symbol.getBody().visit(lookup);

            if (!lookup.nonLocalAssignments().empty()) {
                diags.add(diag::NoOldAlwaysSyntax, symbol.location);
            }
        }
    }
};
} // namespace no_old_always_syntax

using namespace no_old_always_syntax;

class NoOldAlwaysSyntax : public TidyCheck {
public:
    [[maybe_unused]] explicit NoOldAlwaysSyntax(TidyKind kind) : TidyCheck(kind) {}

    bool check(const RootSymbol& root) override {
        MainVisitor visitor(diagnostics);
        root.visit(visitor);
        if (!diagnostics.empty())
            return false;
        return true;
    }

    DiagCode diagCode() const override { return diag::NoOldAlwaysSyntax; }

    std::string diagString() const override { return "Use of old always verilog syntax"; }

    DiagnosticSeverity diagSeverity() const override { return DiagnosticSeverity::Warning; }

    std::string name() const override { return "NoOldAlwaysSyntax "; }

    std::string description() const override { return shortDescription(); }

    std::string shortDescription() const override {
        return "Checks if old always verilog syntax is being use in the design.";
    }
};

REGISTER(NoOldAlwaysSyntax, NoOldAlwaysSyntax, TidyKind::Style)
