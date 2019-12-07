//------------------------------------------------------------------------------
// OperatorExpressions.h
// Definitions for operator expressions.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#pragma once

#include "slang/binding/Expression.h"

namespace slang {

struct PostfixUnaryExpressionSyntax;
struct PrefixUnaryExpressionSyntax;

/// Represents a unary operator expression.
class UnaryExpression : public Expression {
public:
    UnaryOperator op;

    UnaryExpression(UnaryOperator op, const Type& type, Expression& operand,
                    SourceRange sourceRange) :
        Expression(ExpressionKind::UnaryOp, type, sourceRange),
        op(op), operand_(&operand) {}

    const Expression& operand() const { return *operand_; }
    Expression& operand() { return *operand_; }

    ConstantValue evalImpl(EvalContext& context) const;
    bool propagateType(const BindContext& context, const Type& newType);
    bool verifyConstantImpl(EvalContext& context) const;

    void toJson(json& j) const;

    static Expression& fromSyntax(Compilation& compilation,
                                  const PrefixUnaryExpressionSyntax& syntax,
                                  const BindContext& context);

    static Expression& fromSyntax(Compilation& compilation,
                                  const PostfixUnaryExpressionSyntax& syntax,
                                  const BindContext& context);

    static bool isKind(ExpressionKind kind) { return kind == ExpressionKind::UnaryOp; }

private:
    Expression* operand_;
};

struct BinaryExpressionSyntax;

/// Represents a binary operator expression.
class BinaryExpression : public Expression {
public:
    BinaryOperator op;

    BinaryExpression(BinaryOperator op, const Type& type, Expression& left, Expression& right,
                     SourceRange sourceRange) :
        Expression(ExpressionKind::BinaryOp, type, sourceRange),
        op(op), left_(&left), right_(&right) {}

    const Expression& left() const { return *left_; }
    Expression& left() { return *left_; }

    const Expression& right() const { return *right_; }
    Expression& right() { return *right_; }

    ConstantValue evalImpl(EvalContext& context) const;
    bool propagateType(const BindContext& context, const Type& newType);
    bool verifyConstantImpl(EvalContext& context) const;

    void toJson(json& j) const;

    static Expression& fromSyntax(Compilation& compilation, const BinaryExpressionSyntax& syntax,
                                  const BindContext& context);

    static bool isKind(ExpressionKind kind) { return kind == ExpressionKind::BinaryOp; }

private:
    Expression* left_;
    Expression* right_;
};

struct ConditionalExpressionSyntax;

/// Represents a conditional operator expression.
class ConditionalExpression : public Expression {
public:
    ConditionalExpression(const Type& type, Expression& pred, Expression& left, Expression& right,
                          SourceRange sourceRange) :
        Expression(ExpressionKind::ConditionalOp, type, sourceRange),
        pred_(&pred), left_(&left), right_(&right) {}

    const Expression& pred() const { return *pred_; }
    Expression& pred() { return *pred_; }

    const Expression& left() const { return *left_; }
    Expression& left() { return *left_; }

    const Expression& right() const { return *right_; }
    Expression& right() { return *right_; }

    ConstantValue evalImpl(EvalContext& context) const;
    bool propagateType(const BindContext& context, const Type& newType);
    bool verifyConstantImpl(EvalContext& context) const;

    void toJson(json& j) const;

    static Expression& fromSyntax(Compilation& compilation,
                                  const ConditionalExpressionSyntax& syntax,
                                  const BindContext& context, const Type* assignmentTarget);

    static bool isKind(ExpressionKind kind) { return kind == ExpressionKind::ConditionalOp; }

private:
    Expression* pred_;
    Expression* left_;
    Expression* right_;
};

struct InsideExpressionSyntax;

/// Represents a set membership operator expression.
class InsideExpression : public Expression {
public:
    InsideExpression(const Type& type, const Expression& left,
                     span<const Expression* const> rangeList, SourceRange sourceRange) :
        Expression(ExpressionKind::Inside, type, sourceRange),
        left_(&left), rangeList_(rangeList) {}

    const Expression& left() const { return *left_; }

    span<const Expression* const> rangeList() const { return rangeList_; }

    ConstantValue evalImpl(EvalContext& context) const;
    bool verifyConstantImpl(EvalContext& context) const;

    void toJson(json& j) const;

    static Expression& fromSyntax(Compilation& compilation, const InsideExpressionSyntax& syntax,
                                  const BindContext& context);

    static bool isKind(ExpressionKind kind) { return kind == ExpressionKind::Inside; }

private:
    const Expression* left_;
    span<const Expression* const> rangeList_;
};

struct ConcatenationExpressionSyntax;

/// Represents a concatenation expression.
class ConcatenationExpression : public Expression {
public:
    ConcatenationExpression(const Type& type, span<const Expression* const> operands,
                            SourceRange sourceRange) :
        Expression(ExpressionKind::Concatenation, type, sourceRange),
        operands_(operands) {}

    span<const Expression* const> operands() const { return operands_; }

    ConstantValue evalImpl(EvalContext& context) const;
    LValue evalLValueImpl(EvalContext& context) const;
    bool verifyConstantImpl(EvalContext& context) const;

    void toJson(json& j) const;

    static Expression& fromSyntax(Compilation& compilation,
                                  const ConcatenationExpressionSyntax& syntax,
                                  const BindContext& context);

    static bool isKind(ExpressionKind kind) { return kind == ExpressionKind::Concatenation; }

private:
    span<const Expression* const> operands_;
};

struct MultipleConcatenationExpressionSyntax;

/// Represents a replication expression.
class ReplicationExpression : public Expression {
public:
    ReplicationExpression(const Type& type, const Expression& count, Expression& concat,
                          SourceRange sourceRange) :
        Expression(ExpressionKind::Replication, type, sourceRange),
        count_(&count), concat_(&concat) {}

    const Expression& count() const { return *count_; }

    const Expression& concat() const { return *concat_; }
    Expression& concat() { return *concat_; }

    ConstantValue evalImpl(EvalContext& context) const;
    bool verifyConstantImpl(EvalContext& context) const;

    void toJson(json& j) const;

    static Expression& fromSyntax(Compilation& compilation,
                                  const MultipleConcatenationExpressionSyntax& syntax,
                                  const BindContext& context);

    static bool isKind(ExpressionKind kind) { return kind == ExpressionKind::Replication; }

private:
    const Expression* count_;
    Expression* concat_;
};

struct OpenRangeExpressionSyntax;

/// Denotes a range of values by providing expressions for the lower and upper
/// bounds of the range. This expression needs special handling in the various
/// places that allow it, since it doesn't really have a type.
class OpenRangeExpression : public Expression {
public:
    OpenRangeExpression(const Type& type, Expression& left, Expression& right,
                        SourceRange sourceRange) :
        Expression(ExpressionKind::OpenRange, type, sourceRange),
        left_(&left), right_(&right) {}

    const Expression& left() const { return *left_; }
    Expression& left() { return *left_; }

    const Expression& right() const { return *right_; }
    Expression& right() { return *right_; }

    ConstantValue evalImpl(EvalContext& context) const;
    bool propagateType(const BindContext& context, const Type& newType);
    bool verifyConstantImpl(EvalContext& context) const;

    ConstantValue checkInside(EvalContext& context, const ConstantValue& val) const;

    void toJson(json& j) const;

    static Expression& fromSyntax(Compilation& comp, const OpenRangeExpressionSyntax& syntax,
                                  const BindContext& context);

    static bool isKind(ExpressionKind kind) { return kind == ExpressionKind::OpenRange; }

private:
    Expression* left_;
    Expression* right_;
};

} // namespace slang