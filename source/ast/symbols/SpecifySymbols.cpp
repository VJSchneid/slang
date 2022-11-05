//------------------------------------------------------------------------------
// SpecifySymbols.cpp
// Contains specify block symbol definitions
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "slang/ast/symbols/SpecifySymbols.h"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DeclarationsDiags.h"
#include "slang/diagnostics/ExpressionsDiags.h"
#include "slang/diagnostics/StatementsDiags.h"
#include "slang/syntax/AllSyntax.h"

namespace slang::ast {

using namespace parsing;
using namespace syntax;

static void createImplicitNets(const SystemTimingCheckSymbol& timingCheck, const Scope& scope,
                               SmallVector<const Symbol*>& results,
                               SmallSet<string_view, 8>& implicitNetNames) {
    if (timingCheck.timingCheckKind != SystemTimingCheckKind::SetupHold &&
        timingCheck.timingCheckKind != SystemTimingCheckKind::RecRem) {
        return;
    }

    auto& netType = scope.getDefaultNetType();

    // If no default nettype is set, we don't create implicit nets.
    if (netType.isError())
        return;

    auto syntaxPtr = timingCheck.getSyntax();
    ASSERT(syntaxPtr);

    auto& syntax = syntaxPtr->as<SystemTimingCheckSyntax>();
    auto& actualArgs = syntax.args;

    ASTContext context(scope, LookupLocation::max);
    SmallVector<Token, 8> implicitNets;

    for (size_t i = 7; i <= 8; i++) {
        if (actualArgs.size() <= i)
            break;

        if (actualArgs[i]->kind == SyntaxKind::ExpressionTimingCheckArg) {
            auto& exprSyntax = *actualArgs[i]->as<ExpressionTimingCheckArgSyntax>().expr;
            if (exprSyntax.kind == SyntaxKind::IdentifierName)
                Expression::findPotentiallyImplicitNets(exprSyntax, context, implicitNets);
        }
    }

    for (Token t : implicitNets) {
        if (implicitNetNames.emplace(t.valueText()).second) {
            auto& comp = context.getCompilation();
            auto net = comp.emplace<NetSymbol>(t.valueText(), t.location(), netType);
            net->setType(comp.getLogicType());
            results.push_back(net);
        }
    }
}

SpecifyBlockSymbol::SpecifyBlockSymbol(Compilation& compilation, SourceLocation loc) :
    Symbol(SymbolKind::SpecifyBlock, "", loc), Scope(compilation, this) {
}

SpecifyBlockSymbol& SpecifyBlockSymbol::fromSyntax(const Scope& scope,
                                                   const SpecifyBlockSyntax& syntax,
                                                   SmallVector<const Symbol*>& implicitSymbols) {
    auto& comp = scope.getCompilation();
    auto result = comp.emplace<SpecifyBlockSymbol>(comp, syntax.specify.location());
    result->setSyntax(syntax);

    for (auto member : syntax.items)
        result->addMembers(*member);

    SmallSet<string_view, 8> implicitNetNames;

    for (auto member = result->getFirstMember(); member; member = member->getNextSibling()) {
        if (member->kind == SymbolKind::Specparam) {
            // specparams inside specify blocks get visibility in the parent scope as well.
            implicitSymbols.push_back(comp.emplace<TransparentMemberSymbol>(*member));
        }
        else if (member->kind == SymbolKind::SystemTimingCheck) {
            // some system timing checks can create implicit nets
            createImplicitNets(member->as<SystemTimingCheckSymbol>(), scope, implicitSymbols,
                               implicitNetNames);
        }
    }

    return *result;
}

TimingPathSymbol::TimingPathSymbol(SourceLocation loc, ConnectionKind connectionKind,
                                   Polarity polarity, Polarity edgePolarity,
                                   EdgeKind edgeIdentifier) :
    Symbol(SymbolKind::TimingPath, ""sv, loc),
    connectionKind(connectionKind), polarity(polarity), edgePolarity(edgePolarity),
    edgeIdentifier(edgeIdentifier) {
}

TimingPathSymbol& TimingPathSymbol::fromSyntax(const Scope& parent,
                                               const PathDeclarationSyntax& syntax) {
    Polarity polarity;
    switch (syntax.desc->polarityOperator.kind) {
        case TokenKind::Plus:
        case TokenKind::PlusEqual:
            polarity = Polarity::Positive;
            break;
        case TokenKind::Minus:
        case TokenKind::MinusEqual:
            polarity = Polarity::Negative;
            break;
        default:
            polarity = Polarity::Unknown;
            break;
    }

    auto connectionKind = syntax.desc->pathOperator.kind == TokenKind::StarArrow
                              ? ConnectionKind::Full
                              : ConnectionKind::Parallel;

    auto edgeIdentifier = SemanticFacts::getEdgeKind(syntax.desc->edgeIdentifier.kind);

    auto edgePolarity = Polarity::Unknown;
    if (syntax.desc->suffix->kind == SyntaxKind::EdgeSensitivePathSuffix) {
        auto& esps = syntax.desc->suffix->as<EdgeSensitivePathSuffixSyntax>();
        switch (esps.polarityOperator.kind) {
            case TokenKind::Plus:
            case TokenKind::PlusColon:
                edgePolarity = Polarity::Positive;
                break;
            case TokenKind::Minus:
            case TokenKind::MinusColon:
                edgePolarity = Polarity::Negative;
                break;
            default:
                break;
        }
    }

    auto& comp = parent.getCompilation();
    auto result = comp.emplace<TimingPathSymbol>(syntax.getFirstToken().location(), connectionKind,
                                                 polarity, edgePolarity, edgeIdentifier);
    result->setSyntax(syntax);
    return *result;
}

TimingPathSymbol& TimingPathSymbol::fromSyntax(const Scope& parent,
                                               const IfNonePathDeclarationSyntax& syntax) {
    auto& result = fromSyntax(parent, *syntax.path);
    result.setSyntax(syntax);
    result.isStateDependent = true;
    return result;
}

TimingPathSymbol& TimingPathSymbol::fromSyntax(const Scope& parent,
                                               const ConditionalPathDeclarationSyntax& syntax) {
    auto& result = fromSyntax(parent, *syntax.path);
    result.setSyntax(syntax);
    result.isStateDependent = true;
    return result;
}

static bool checkPathTerminal(const ValueSymbol& terminal, const Scope* specifyParent,
                              ASTContext& context, bool isSource, SourceRange sourceRange) {
    // Type must be integral.
    auto& type = terminal.getType();
    if (!type.isIntegral()) {
        if (!type.isError())
            context.addDiag(diag::InvalidSpecifyType, sourceRange) << terminal.name << type;
        return false;
    }

    auto reportErr = [&] {
        auto code = isSource ? diag::InvalidSpecifySource : diag::InvalidSpecifyDest;
        auto& diag = context.addDiag(code, sourceRange) << terminal.name;
        diag.addNote(diag::NoteDeclarationHere, terminal.location);
    };

    // Inputs must be nets (or modport ports) and outputs must
    // be nets or variables (or modport ports).
    if (terminal.kind != SymbolKind::Net && terminal.kind != SymbolKind::ModportPort &&
        (terminal.kind != SymbolKind::Variable || isSource)) {
        reportErr();
        return false;
    }

    if (terminal.kind == SymbolKind::ModportPort) {
        // Check that the modport port has the correct direction.
        auto dir = terminal.as<ModportPortSymbol>().direction;
        if (dir != ArgumentDirection::InOut && ((isSource && dir != ArgumentDirection::In) ||
                                                (!isSource && dir != ArgumentDirection::Out))) {
            reportErr();
            return false;
        }
        return true;
    }

    auto terminalParentScope = terminal.getParentScope();
    ASSERT(terminalParentScope);

    auto& terminalParent = terminalParentScope->asSymbol();
    if (terminalParent.kind == SymbolKind::InstanceBody &&
        terminalParent.as<InstanceBodySymbol>().getDefinition().definitionKind ==
            DefinitionKind::Interface) {
        // If the signal is part of an interface then the only way we could have accessed
        // it is through an interface port, in which case the direction is "inout" and
        // therefore fine no matter whether this is an input or output terminal.
        return true;
    }

    // If we get here then the terminal must be a member of the module containing
    // our parent specify block.
    ASSERT(specifyParent);
    if (&terminalParent != &specifyParent->asSymbol()) {
        context.addDiag(diag::InvalidSpecifyPath, sourceRange);
        return false;
    }

    // Check that the terminal is connected to a module port and that
    // the direction is correct.
    for (auto portRef = terminal.getFirstPortBackref(); portRef;
         portRef = portRef->getNextBackreference()) {
        auto dir = portRef->port->direction;
        if (dir == ArgumentDirection::InOut || (isSource && dir == ArgumentDirection::In) ||
            (!isSource && dir == ArgumentDirection::Out)) {
            return true;
        }
    }

    reportErr();
    return false;
}

static const Expression* bindTerminal(const ExpressionSyntax& syntax, bool isSource,
                                      const Scope* parentParent, ASTContext& context) {
    auto expr = &Expression::bind(syntax, context);
    if (expr->bad())
        return nullptr;

    switch (expr->kind) {
        case ExpressionKind::ElementSelect:
            expr = &expr->as<ElementSelectExpression>().value();
            break;
        case ExpressionKind::RangeSelect:
            expr = &expr->as<RangeSelectExpression>().value();
            break;
        default:
            break;
    }

    if (expr->kind != ExpressionKind::NamedValue) {
        auto code = (expr->kind == ExpressionKind::ElementSelect ||
                     expr->kind == ExpressionKind::RangeSelect)
                        ? diag::SpecifyPathMultiDim
                        : diag::InvalidSpecifyPath;
        context.addDiag(code, syntax.sourceRange());
    }
    else {
        auto& symbol = expr->as<NamedValueExpression>().symbol;
        if (checkPathTerminal(symbol, parentParent, context, isSource, expr->sourceRange))
            return expr;
    }

    return nullptr;
}

static span<const Expression* const> bindTerminals(
    const SeparatedSyntaxList<NameSyntax>& syntaxList, bool isSource, const Scope* parentParent,
    ASTContext& context) {

    SmallVector<const Expression*> results;
    for (auto exprSyntax : syntaxList) {
        auto expr = bindTerminal(*exprSyntax, isSource, parentParent, context);
        if (expr)
            results.push_back(expr);
    }
    return results.copy(context.getCompilation());
}

// Only a subset of expressions are allowed to be used in specify path conditions.
struct SpecifyConditionVisitor {
    const ASTContext& context;
    const Scope* specifyParentScope;
    bool hasError = false;

    SpecifyConditionVisitor(const ASTContext& context, const Scope* specifyParentScope) :
        context(context), specifyParentScope(specifyParentScope) {}

    template<typename T>
    void visit(const T& expr) {
        if constexpr (std::is_base_of_v<Expression, T>) {
            switch (expr.kind) {
                case ExpressionKind::NamedValue:
                    if (auto sym = expr.getSymbolReference()) {
                        // Specparams are always allowed.
                        if (sym->kind == SymbolKind::Specparam || hasError)
                            break;

                        // Other references must be locally defined nets or variables.
                        if ((sym->kind != SymbolKind::Net && sym->kind != SymbolKind::Variable) ||
                            sym->getParentScope() != specifyParentScope) {
                            auto& diag = context.addDiag(diag::SpecifyPathBadReference,
                                                         expr.sourceRange);
                            diag << sym->name;
                            diag.addNote(diag::NoteDeclarationHere, sym->location);
                            hasError = true;
                        }
                    }
                    break;
                case ExpressionKind::ElementSelect:
                case ExpressionKind::RangeSelect:
                case ExpressionKind::Call:
                case ExpressionKind::MinTypMax:
                case ExpressionKind::Concatenation:
                case ExpressionKind::Replication:
                case ExpressionKind::ConditionalOp:
                case ExpressionKind::UnaryOp:
                case ExpressionKind::BinaryOp:
                case ExpressionKind::Conversion:
                    if constexpr (is_detected_v<ASTDetectors::visitExprs_t, T,
                                                SpecifyConditionVisitor>)
                        expr.visitExprs(*this);

                    if (expr.kind == ExpressionKind::UnaryOp) {
                        switch (expr.template as<UnaryExpression>().op) {
                            case UnaryOperator::BitwiseNot:
                            case UnaryOperator::BitwiseAnd:
                            case UnaryOperator::BitwiseOr:
                            case UnaryOperator::BitwiseXor:
                            case UnaryOperator::BitwiseNand:
                            case UnaryOperator::BitwiseNor:
                            case UnaryOperator::BitwiseXnor:
                            case UnaryOperator::LogicalNot:
                                break;
                            default:
                                reportError(expr.sourceRange);
                        }
                    }
                    else if (expr.kind == ExpressionKind::BinaryOp) {
                        switch (expr.template as<BinaryExpression>().op) {
                            case BinaryOperator::BinaryAnd:
                            case BinaryOperator::BinaryOr:
                            case BinaryOperator::BinaryXor:
                            case BinaryOperator::BinaryXnor:
                            case BinaryOperator::Equality:
                            case BinaryOperator::Inequality:
                            case BinaryOperator::LogicalAnd:
                            case BinaryOperator::LogicalOr:
                                break;
                            default:
                                reportError(expr.sourceRange);
                        }
                    }
                    else if (expr.kind == ExpressionKind::Conversion) {
                        if (!expr.template as<ConversionExpression>().isImplicit())
                            reportError(expr.sourceRange);
                    }
                    break;
                case ExpressionKind::IntegerLiteral:
                case ExpressionKind::RealLiteral:
                    break;
                default:
                    reportError(expr.sourceRange);
                    break;
            }
        }
    }

    void reportError(SourceRange sourceRange) {
        if (!hasError) {
            context.addDiag(diag::SpecifyPathConditionExpr, sourceRange);
            hasError = true;
        }
    }

    void visitInvalid(const Expression&) {}
    void visitInvalid(const AssertionExpr&) {}
};

void TimingPathSymbol::resolve() const {
    isResolved = true;

    auto syntaxPtr = getSyntax();
    auto parent = getParentScope();
    ASSERT(syntaxPtr && parent);

    auto parentParent = parent->asSymbol().getParentScope();
    auto& comp = parent->getCompilation();
    ASTContext context(*parent, LookupLocation::after(*this),
                       ASTFlags::NonProcedural | ASTFlags::SpecifyBlock);

    if (syntaxPtr->kind == SyntaxKind::IfNonePathDeclaration) {
        syntaxPtr = syntaxPtr->as<IfNonePathDeclarationSyntax>().path;
    }
    else if (syntaxPtr->kind == SyntaxKind::ConditionalPathDeclaration) {
        auto& conditional = syntaxPtr->as<ConditionalPathDeclarationSyntax>();
        syntaxPtr = conditional.path;

        conditionExpr = &Expression::bind(*conditional.predicate, context);
        if (context.requireBooleanConvertible(*conditionExpr)) {
            SpecifyConditionVisitor visitor(context, parentParent);
            conditionExpr->visit(visitor);
        }
    }

    auto& syntax = syntaxPtr->as<PathDeclarationSyntax>();
    inputs = bindTerminals(syntax.desc->inputs, true, parentParent, context);

    if (syntax.desc->suffix->kind == SyntaxKind::EdgeSensitivePathSuffix) {
        auto& esps = syntax.desc->suffix->as<EdgeSensitivePathSuffixSyntax>();
        outputs = bindTerminals(esps.outputs, false, parentParent, context);

        // This expression is apparently allowed to be anything the user wants.
        edgeSourceExpr = &Expression::bind(*esps.expr, context);
    }
    else {
        outputs = bindTerminals(syntax.desc->suffix->as<SimplePathSuffixSyntax>().outputs, false,
                                parentParent, context);
    }

    // Verify that input and output sizes match for parallel connections.
    // Parallel connections only allow one input and one output.
    if (connectionKind == ConnectionKind::Parallel && inputs.size() == 1 && outputs.size() == 1) {
        if (inputs[0]->type->getBitWidth() != outputs[0]->type->getBitWidth()) {
            auto& diag = context.addDiag(diag::ParallelPathWidth,
                                         syntax.desc->pathOperator.range());
            diag << inputs[0]->sourceRange << outputs[0]->sourceRange;
            diag << *inputs[0]->type << *outputs[0]->type;
        }
    }

    // Bind all delay values.
    SmallVector<const Expression*> delayBuf;
    for (auto delaySyntax : syntax.delays) {
        auto& expr = Expression::bind(*delaySyntax, context);
        if (!expr.bad()) {
            if (!expr.type->isNumeric()) {
                context.addDiag(diag::DelayNotNumeric, expr.sourceRange) << *expr.type;
                continue;
            }

            delayBuf.push_back(&expr);
            context.eval(expr);
        }
    }

    delays = delayBuf.copy(comp);
}

static string_view toString(TimingPathSymbol::Polarity polarity) {
    switch (polarity) {
        case TimingPathSymbol::Polarity::Unknown:
            return "Unknown"sv;
        case TimingPathSymbol::Polarity::Positive:
            return "Positive"sv;
        case TimingPathSymbol::Polarity::Negative:
            return "Negative"sv;
        default:
            ASSUME_UNREACHABLE;
    }
}

void TimingPathSymbol::serializeTo(ASTSerializer& serializer) const {
    serializer.write("connectionKind",
                     connectionKind == ConnectionKind::Full ? "Full"sv : "Parallel"sv);
    serializer.write("polarity", toString(polarity));
    serializer.write("edgePolarity", toString(edgePolarity));
    serializer.write("edgeIdentifier", toString(edgeIdentifier));
    serializer.write("isStateDependent", isStateDependent);

    if (auto expr = getEdgeSourceExpr())
        serializer.write("edgeSourceExpr", *expr);

    if (auto expr = getConditionExpr())
        serializer.write("conditionExpr", *expr);

    serializer.startArray("inputs");
    for (auto expr : getInputs())
        serializer.serialize(*expr);
    serializer.endArray();

    serializer.startArray("outputs");
    for (auto expr : getOutputs())
        serializer.serialize(*expr);
    serializer.endArray();

    serializer.startArray("delays");
    for (auto expr : getDelays())
        serializer.serialize(*expr);
    serializer.endArray();
}

PulseStyleSymbol::PulseStyleSymbol(SourceLocation loc, PulseStyleKind pulseStyleKind) :
    Symbol(SymbolKind::PulseStyle, ""sv, loc), pulseStyleKind(pulseStyleKind) {
}

PulseStyleSymbol& PulseStyleSymbol::fromSyntax(const Scope& parent,
                                               const PulseStyleDeclarationSyntax& syntax) {
    auto pulseStyleKind = SemanticFacts::getPulseStyleKind(syntax.keyword.kind);

    auto& comp = parent.getCompilation();
    auto result = comp.emplace<PulseStyleSymbol>(syntax.getFirstToken().location(), pulseStyleKind);
    result->setSyntax(syntax);
    return *result;
}

void PulseStyleSymbol::resolve() const {
    isResolved = true;

    auto syntaxPtr = getSyntax();
    auto parent = getParentScope();
    ASSERT(syntaxPtr && parent);

    auto parentParent = parent->asSymbol().getParentScope();
    ASTContext context(*parent, LookupLocation::after(*this),
                       ASTFlags::NonProcedural | ASTFlags::SpecifyBlock);

    auto& syntax = syntaxPtr->as<PulseStyleDeclarationSyntax>();
    terminals = bindTerminals(syntax.inputs, false, parentParent, context);
}

void PulseStyleSymbol::serializeTo(ASTSerializer& serializer) const {
    serializer.write("pulseStyleKind", toString(pulseStyleKind));
}

struct SystemTimingCheckArgDef {
    enum ArgKind {
        Event,
        Limit,
        Notifier,
        Condition,
        DelayedRef,
        EventFlag,
        RemainFlag,
        Offset
    } kind;

    bool requirePositive = false;
    int signalRef = -1;
    bool requireEdge = false;
    bool allowEmpty = true;
};

struct SystemTimingCheckDef {
    SystemTimingCheckKind kind;
    size_t minArgs;
    std::vector<SystemTimingCheckArgDef> args;
};

static flat_hash_map<string_view, SystemTimingCheckDef> createTimingCheckDefs() {
    using Arg = SystemTimingCheckArgDef;

    SystemTimingCheckDef setup{
        SystemTimingCheckKind::Setup,
        3,
        {{Arg::Event}, {Arg::Event}, {Arg::Limit, /* requirePositive */ true}, {Arg::Notifier}}};

    SystemTimingCheckDef hold{
        SystemTimingCheckKind::Hold,
        3,
        {{Arg::Event}, {Arg::Event}, {Arg::Limit, /* requirePositive */ true}, {Arg::Notifier}}};

    SystemTimingCheckDef setupHold{SystemTimingCheckKind::SetupHold,
                                   4,
                                   {{Arg::Event},
                                    {Arg::Event},
                                    {Arg::Limit},
                                    {Arg::Limit},
                                    {Arg::Notifier},
                                    {Arg::Condition},
                                    {Arg::Condition},
                                    {Arg::DelayedRef, /* requirePositive */ false, 0},
                                    {Arg::DelayedRef, /* requirePositive */ false, 1}}};

    SystemTimingCheckDef recovery{
        SystemTimingCheckKind::Recovery,
        3,
        {{Arg::Event}, {Arg::Event}, {Arg::Limit, /* requirePositive */ true}, {Arg::Notifier}}};

    SystemTimingCheckDef removal{
        SystemTimingCheckKind::Removal,
        3,
        {{Arg::Event}, {Arg::Event}, {Arg::Limit, /* requirePositive */ true}, {Arg::Notifier}}};

    SystemTimingCheckDef recRem{SystemTimingCheckKind::RecRem,
                                4,
                                {{Arg::Event},
                                 {Arg::Event},
                                 {Arg::Limit},
                                 {Arg::Limit},
                                 {Arg::Notifier},
                                 {Arg::Condition},
                                 {Arg::Condition},
                                 {Arg::DelayedRef, /* requirePositive */ false, 0},
                                 {Arg::DelayedRef, /* requirePositive */ false, 1}}};

    SystemTimingCheckDef skew{
        SystemTimingCheckKind::Skew,
        3,
        {{Arg::Event}, {Arg::Event}, {Arg::Limit, /* requirePositive */ true}, {Arg::Notifier}}};

    SystemTimingCheckDef timeSkew{SystemTimingCheckKind::TimeSkew,
                                  3,
                                  {{Arg::Event},
                                   {Arg::Event},
                                   {Arg::Limit, /* requirePositive */ true},
                                   {Arg::Notifier},
                                   {Arg::EventFlag},
                                   {Arg::RemainFlag}}};

    SystemTimingCheckDef fullSkew{SystemTimingCheckKind::FullSkew,
                                  4,
                                  {{Arg::Event},
                                   {Arg::Event},
                                   {Arg::Limit, /* requirePositive */ true},
                                   {Arg::Limit, /* requirePositive */ true},
                                   {Arg::Notifier},
                                   {Arg::EventFlag},
                                   {Arg::RemainFlag}}};

    SystemTimingCheckDef period{SystemTimingCheckKind::Period,
                                2,
                                {{Arg::Event, false, -1, /* requireEdge */ true},
                                 {Arg::Limit, /* requirePositive */ true},
                                 {Arg::Notifier}}};

    SystemTimingCheckDef width{SystemTimingCheckKind::Width,
                               2,
                               {{Arg::Event, false, -1, /* requireEdge */ true},
                                {Arg::Limit, /* requirePositive */ true},
                                {Arg::Limit, /* requirePositive */ true, -1, false,
                                 /* allowEmpty */ false},
                                {Arg::Notifier}}};

    SystemTimingCheckDef noChange{
        SystemTimingCheckKind::NoChange,
        4,
        {{Arg::Event}, {Arg::Event}, {Arg::Offset}, {Arg::Offset}, {Arg::Notifier}}};

    return {{"$setup"sv, std::move(setup)},         {"$hold"sv, std::move(hold)},
            {"$setuphold"sv, std::move(setupHold)}, {"$recovery"sv, std::move(recovery)},
            {"$removal"sv, std::move(removal)},     {"$recrem"sv, std::move(recRem)},
            {"$skew"sv, std::move(skew)},           {"$timeskew"sv, std::move(timeSkew)},
            {"$fullskew"sv, std::move(fullSkew)},   {"$period"sv, std::move(period)},
            {"$width"sv, std::move(width)},         {"$nochange"sv, std::move(noChange)}};
}

static const flat_hash_map<string_view, SystemTimingCheckDef> SystemTimingCheckDefs =
    createTimingCheckDefs();

SystemTimingCheckSymbol::SystemTimingCheckSymbol(SourceLocation loc,
                                                 const SystemTimingCheckDef* def) :
    Symbol(SymbolKind::SystemTimingCheck, ""sv, loc),
    def(def) {
    timingCheckKind = def ? def->kind : SystemTimingCheckKind::Unknown;
}

SystemTimingCheckSymbol& SystemTimingCheckSymbol::fromSyntax(
    const Scope& parent, const SystemTimingCheckSyntax& syntax) {

    const SystemTimingCheckDef* def;
    if (auto it = SystemTimingCheckDefs.find(syntax.name.valueText());
        it != SystemTimingCheckDefs.end()) {
        def = &it->second;
    }
    else {
        parent.addDiag(diag::UnknownSystemTimingCheck, syntax.name.range())
            << syntax.name.valueText();
        def = nullptr;
    }

    auto& comp = parent.getCompilation();
    auto result = comp.emplace<SystemTimingCheckSymbol>(syntax.getFirstToken().location(), def);
    result->setSyntax(syntax);
    return *result;
}

void SystemTimingCheckSymbol::resolve() const {
    isResolved = true;
    if (!def)
        return;

    auto syntaxPtr = getSyntax();
    auto parent = getParentScope();
    ASSERT(syntaxPtr && parent);

    auto parentParent = parent->asSymbol().getParentScope();
    auto& comp = parent->getCompilation();
    ASTContext context(*parent, LookupLocation::after(*this),
                       ASTFlags::NonProcedural | ASTFlags::SpecifyBlock);

    auto& syntax = syntaxPtr->as<SystemTimingCheckSyntax>();
    auto& actualArgs = syntax.args;
    auto& formalArgs = def->args;

    if (actualArgs.size() < def->minArgs) {
        auto& diag = context.addDiag(diag::TooFewArguments, syntax.sourceRange());
        diag << syntax.name.valueText();
        diag << def->minArgs << actualArgs.size();
        return;
    }

    if (actualArgs.size() > formalArgs.size()) {
        auto& diag = context.addDiag(diag::TooManyArguments, syntax.sourceRange());
        diag << syntax.name.valueText();
        diag << formalArgs.size();
        diag << actualArgs.size();
        return;
    }

    SmallVector<Arg> argBuf;
    for (size_t i = 0; i < actualArgs.size(); i++) {
        auto& formal = formalArgs[i];
        auto& actual = *actualArgs[i];
        if (actual.kind == SyntaxKind::EmptyTimingCheckArg) {
            if (i < def->minArgs || !formal.allowEmpty)
                context.addDiag(diag::EmptyArgNotAllowed, actualArgs[i]->sourceRange());
            argBuf.emplace_back();
            continue;
        }

        if (actual.kind == SyntaxKind::TimingCheckEventArg &&
            formal.kind != SystemTimingCheckArgDef::Event) {
            context.addDiag(diag::TimingCheckEventNotAllowed, actual.sourceRange());
            argBuf.emplace_back();
            continue;
        }

        switch (formal.kind) {
            case SystemTimingCheckArgDef::Limit:
            case SystemTimingCheckArgDef::EventFlag: {
                // Constant integral expression, can't be min:typ:max
                auto& expr = Expression::bind(*actual.as<ExpressionTimingCheckArgSyntax>().expr,
                                              context);
                if (expr.kind == ExpressionKind::MinTypMax)
                    context.addDiag(diag::MinTypMaxNotAllowed, expr.sourceRange);

                auto val = context.evalInteger(expr);
                if (formal.requirePositive)
                    context.requirePositive(val, expr.sourceRange);

                argBuf.emplace_back(expr);
                break;
            }
            case SystemTimingCheckArgDef::Condition: {
                // Non-constant integral expression, can be min:typ:max
                auto& expr = Expression::bind(*actual.as<ExpressionTimingCheckArgSyntax>().expr,
                                              context);
                context.requireIntegral(expr);
                argBuf.emplace_back(expr);
                break;
            }
            case SystemTimingCheckArgDef::RemainFlag:
            case SystemTimingCheckArgDef::Offset: {
                // Constant integral expression, can be min:typ:max
                auto& expr = Expression::bind(*actual.as<ExpressionTimingCheckArgSyntax>().expr,
                                              context);
                context.evalInteger(expr);
                argBuf.emplace_back(expr);
                break;
            }
            case SystemTimingCheckArgDef::Notifier: {
                // Needs to be a simple identifier, referencing an integral lvalue
                auto& exprSyntax = *actual.as<ExpressionTimingCheckArgSyntax>().expr;
                if (exprSyntax.kind != SyntaxKind::IdentifierName) {
                    context.addDiag(diag::InvalidTimingCheckNotifierArg, actual.sourceRange());
                    argBuf.emplace_back();
                    break;
                }

                ASTContext nonContinuous = context;
                nonContinuous.flags &= ~ASTFlags::NonProcedural;

                auto& expr = Expression::bindLValue(exprSyntax, comp.getLogicType(),
                                                    exprSyntax.getFirstToken().location(),
                                                    nonContinuous, /* isInout */ false);
                argBuf.emplace_back(expr);
                break;
            }
            case SystemTimingCheckArgDef::Event:
                if (actual.kind == SyntaxKind::ExpressionTimingCheckArg) {
                    auto expr = bindTerminal(*actual.as<ExpressionTimingCheckArgSyntax>().expr,
                                             /* isSource */ true, parentParent, context);
                    if (!expr)
                        argBuf.emplace_back();
                    else
                        argBuf.emplace_back(*expr);
                }
                else {
                    auto& eventArg = actual.as<TimingCheckEventArgSyntax>();
                    auto expr = bindTerminal(*eventArg.terminal,
                                             /* isSource */ true, parentParent, context);

                    const Expression* condition = nullptr;
                    if (eventArg.condition) {
                        condition = &Expression::bind(*eventArg.condition->expr, context);
                        context.requireIntegral(*condition);
                    }

                    auto edge = SemanticFacts::getEdgeKind(eventArg.edge.kind);

                    SmallVector<EdgeDescriptor> edgeDescriptors;
                    if (eventArg.controlSpecifier) {
                        for (auto descSyntax : eventArg.controlSpecifier->descriptors) {
                            auto t1 = descSyntax->t1.rawText();
                            auto t2 = descSyntax->t2.rawText();
                            if (t1.length() + t2.length() != 2)
                                continue;

                            char edges[2] = {};
                            memcpy(edges, t1.data(), t1.length());
                            if (!t2.empty())
                                memcpy(edges + t1.length(), t2.data(), t2.length());

                            edgeDescriptors.push_back({edges[0], edges[1]});
                        }
                    }

                    argBuf.emplace_back(*expr, condition, edge, edgeDescriptors.copy(comp));
                }

                if (formal.requireEdge && argBuf.back().expr &&
                    argBuf.back().edge == EdgeKind::None) {
                    context.addDiag(diag::TimingCheckEventEdgeRequired,
                                    argBuf.back().expr->sourceRange)
                        << syntax.name.valueText();
                }
                break;
            case SystemTimingCheckArgDef::DelayedRef: {
                ASSERT(formal.signalRef >= 0);
                auto signalExpr = argBuf[formal.signalRef].expr;
                if (!signalExpr) {
                    argBuf.emplace_back();
                    break;
                }

                // Integral lvalue, can create implicit nets (handled in SpecifyBlock factory).
                auto& exprSyntax = *actual.as<ExpressionTimingCheckArgSyntax>().expr;
                auto& expr = Expression::bindLValue(exprSyntax, *signalExpr->type,
                                                    exprSyntax.getFirstToken().location(), context,
                                                    /* isInout */ false);
                argBuf.emplace_back(expr);
                break;
            }
            default:
                ASSUME_UNREACHABLE;
        }
    }

    args = argBuf.copy(comp);
}

void SystemTimingCheckSymbol::serializeTo(ASTSerializer& serializer) const {
    serializer.write("timingCheckKind", toString(timingCheckKind));
}

} // namespace slang::ast
