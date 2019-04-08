/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RamOperation.h
 *
 * Defines the Operation of a relational algebra query.
 *
 ***********************************************************************/

#pragma once

#include "RamCondition.h"
#include "RamExpression.h"
#include "RamNode.h"
#include "RamRelation.h"
#include "RamTypes.h"
#include "Util.h"
#include <cassert>
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace souffle {

/**
 * Abstract class for a relational algebra operation
 */
class RamOperation : public RamNode {
public:
    RamOperation(RamNodeType type) : RamNode(type) {}

    /** Print */
    virtual void print(std::ostream& os, int tabpos) const = 0;

    /** Pretty print */
    void print(std::ostream& os) const override {
        print(os, 0);
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override = 0;

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override = 0;

    /** Create clone */
    RamOperation* clone() const override = 0;

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override = 0;
};

/**
 * Abstract class for nesting operations
 */
class RamNestedOperation : public RamOperation {
public:
    RamNestedOperation(RamNodeType type, std::unique_ptr<RamOperation> nested, std::string profileText = "")
            : RamOperation(type), nestedOperation(std::move(nested)), profileText(std::move(profileText)) {}

    /** Get nested operation */
    RamOperation& getOperation() const {
        assert(nullptr != nestedOperation);
        return *nestedOperation;
    }

    /** Get profile text */
    const std::string& getProfileText() const {
        return profileText;
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        nestedOperation->print(os, tabpos + 1);
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {nestedOperation.get()};
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        nestedOperation = map(std::move(nestedOperation));
    }

protected:
    /** Nested operation */
    std::unique_ptr<RamOperation> nestedOperation;

    /** Profile text */
    const std::string profileText;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamNestedOperation*>(&node));
        const auto& other = static_cast<const RamNestedOperation&>(node);
        return getOperation() == other.getOperation() && getProfileText() == other.getProfileText();
    }
};

/**
 * Abstract class for relation searches and lookups
 */
class RamSearch : public RamNestedOperation {
public:
    RamSearch(RamNodeType type, size_t ident, std::unique_ptr<RamOperation> nested,
            std::string profileText = "")
            : RamNestedOperation(type, std::move(nested), std::move(profileText)), identifier(ident) {}

    /** Get identifier */
    std::size_t getIdentifier() const {
        return identifier;
    }

protected:
    /** Identifier for the tuple */
    const size_t identifier;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamSearch*>(&node));
        const auto& other = static_cast<const RamSearch&>(node);
        return RamNestedOperation::equal(other) && getIdentifier() == other.getIdentifier();
    }
};

/**
 * Abstract class for relation searches
 */
class RamRelationSearch : public RamSearch {
public:
    RamRelationSearch(RamNodeType type, std::unique_ptr<RamRelationReference> relRef, size_t ident,
            std::unique_ptr<RamOperation> nested, std::string profileText = "")
            : RamSearch(type, ident, std::move(nested), std::move(profileText)),
              relationRef(std::move(relRef)) {}

    const RamRelation& getRelation() const {
        return *relationRef->get();
    }

    std::vector<const RamNode*> getChildNodes() const override {
        return {nestedOperation.get(), relationRef.get() };
    }

    void apply(const RamNodeMapper& map) override {
        RamSearch::apply(map);
        relationRef = map(std::move(relationRef));
    }

protected:
    /** Search relation */
    std::unique_ptr<RamRelationReference> relationRef;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamRelationSearch*>(&node));
        const auto& other = static_cast<const RamRelationSearch&>(node);
        return RamSearch::equal(other) && getRelation() == other.getRelation();
    }
};

/**
 * Relation Scan
 *
 * Iterate all tuples of a relation
 */
class RamScan : public RamRelationSearch {
public:
    RamScan(std::unique_ptr<RamRelationReference> rel, size_t ident, std::unique_ptr<RamOperation> nested,
            std::string profileText = "")
            : RamRelationSearch(RN_Scan, std::move(rel), ident, std::move(nested), std::move(profileText)) {}

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times(" ", tabpos);
        os << "FOR t" << getIdentifier();
        os << " in " << getRelation().getName() << std::endl;
        RamRelationSearch::print(os, tabpos + 1);
    }

    std::vector<const RamNode*> getChildNodes() const override {
        return {nestedOperation.get(), condition.get(), relationRef.get() };
    }

    RamScan* clone() const override {
        return new RamScan(std::unique_ptr<RamRelationReference>(relationRef->clone()), getIdentifier(),
                       std::unique_ptr<RamOperation>(getOperation().clone()), getProfileText());
    }
};

class RamChoice : public RamRelationSearch {
public:
    RamChoice(std::unique_ptr<RamRelationReference> rel, size_t ident, std::unique_ptr<RamCondition> cond, std::unique_ptr<RamOperation> nested,
            std::string profileText = "")
            : RamRelationSearch(RN_Scan, std::move(rel), ident, std::move(nested), std::move(profileText)), 
              condition(cond) {}

    void print(std::ostream& os, int tabpos) const override {
        os << times(" ", tabpos);
        os << "CHOICE t" << getIdentifier();
        os << " IN " << getRelation().getName();
        os << " WHERE " << getCondition();
        RamRelationSearch::print(os, tabpos + 1);
    }

    /** get condition */ 
    const RamCondition &getCondition() const {
        return condition;
    }

    void apply(const RamNodeMapper& map) override {
        RamRelationSearch::apply(map);
        condition = map(std::move(condition));
    }

    RamScan* clone() const override {
        return new RamChoice(std::unique_ptr<RamRelationReference>(relationRef->clone()), getIdentifier(), getCondition(),
                std::unique_ptr<RamOperation>(getOperation().clone()), getProfileText());
    }

    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamRelationSearch*>(&node));
        const auto& other = static_cast<const RamRelationSearch&>(node);
        return RamRelationSearch::equal(other) && getCondition() == other.getCondition();
    }
protected:
    std::unique_ptr<RamCondition> condition; 
};

/**
 * Relation Scan with Index
 *
 * Search for tuples of a relation matching a criteria
 */
class RamIndexScan : public RamRelationSearch {
public:
    RamIndexScan(std::unique_ptr<RamRelationReference> r, size_t ident,
            std::vector<std::unique_ptr<RamExpression>> queryPattern, std::unique_ptr<RamOperation> nested,
            std::string profileText = "")
            : RamRelationSearch(RN_IndexScan, std::move(r), ident, std::move(nested), std::move(profileText)),
              queryPattern(std::move(queryPattern)) {
        assert(getRangePattern().size() == getRelation().getArity());
    }

    /** Get range pattern */
    std::vector<RamExpression*> getRangePattern() const {
        return toPtrVector(queryPattern);
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        const RamRelation& rel = getRelation();
        os << times(" ", tabpos);
        os << "SEARCH " << rel.getName() << " AS t" << getIdentifier() << " ON INDEX ";
        bool first = true;
        for (unsigned int i = 0; i < rel.getArity(); ++i) {
            if (queryPattern[i] != nullptr) {
                if (first) {
                    first = false;
                } else {
                    os << " and ";
                }
                os << "t" << getIdentifier() << "." << rel.getArg(i) << "=";
                queryPattern[i]->print(os);
            }
        }
        os << std::endl;
        RamRelationSearch::print(os, tabpos + 1);
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        auto res = RamRelationSearch::getChildNodes();
        for (auto& cur : queryPattern) {
            if (cur != nullptr) {
                res.push_back(cur.get());
            }
        }
        return res;
    }

    /** Create clone */
    RamIndexScan* clone() const override {
        std::vector<std::unique_ptr<RamExpression>> resQueryPattern(queryPattern.size());
        for (unsigned int i = 0; i < queryPattern.size(); ++i) {
            if (nullptr != queryPattern[i]) {
                resQueryPattern[i] = std::unique_ptr<RamExpression>(queryPattern[i]->clone());
            }
        }
        RamIndexScan* res = new RamIndexScan(std::unique_ptr<RamRelationReference>(relationRef->clone()),
                getIdentifier(), std::move(resQueryPattern),
                std::unique_ptr<RamOperation>(getOperation().clone()), getProfileText());
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        RamRelationSearch::apply(map);
        for (auto& cur : queryPattern) {
            if (cur != nullptr) {
                cur = map(std::move(cur));
            }
        }
    }

protected:
    /** Values of index per column of table (if indexable) */
    std::vector<std::unique_ptr<RamExpression>> queryPattern;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamIndexScan*>(&node));
        const auto& other = static_cast<const RamIndexScan&>(node);
        return RamRelationSearch::equal(other) && equal_targets(queryPattern, other.queryPattern);
    }
};

/**
 * Record lookup
 */
class RamUnpackRecord : public RamSearch {
public:
    RamUnpackRecord(std::unique_ptr<RamOperation> nested, size_t ident, size_t ref_level, size_t ref_pos,
            size_t arity)
            : RamSearch(RN_UnpackRecord, ident, std::move(nested)), refLevel(ref_level), refPos(ref_pos),
              arity(arity) {}

    /** Get reference level */
    std::size_t getReferenceLevel() const {
        return refLevel;
    }

    /** Get reference position */
    std::size_t getReferencePosition() const {
        return refPos;
    }

    /** Get arity */
    std::size_t getArity() const {
        return arity;
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times(" ", tabpos) << "UNPACK env(t" << refLevel << ", i" << refPos << ") INTO t"
           << getIdentifier() << std::endl;
        RamSearch::print(os, tabpos + 1);
    }

    /** Create clone */
    RamUnpackRecord* clone() const override {
        RamUnpackRecord* res = new RamUnpackRecord(std::unique_ptr<RamOperation>(getOperation().clone()),
                getIdentifier(), refLevel, refPos, arity);
        return res;
    }

protected:
    /** Level of the tuple containing record reference */
    const size_t refLevel;

    /** Position of the tuple containing record reference */
    const size_t refPos;

    /** Arity of the unpacked tuple */
    const size_t arity;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamUnpackRecord*>(&node));
        const auto& other = static_cast<const RamUnpackRecord&>(node);
        return RamSearch::equal(other) && getReferencePosition() == other.getReferencePosition() &&
               getReferenceLevel() == other.getReferenceLevel() && getArity() == other.getArity();
    }
};

/**
 * Aggregation
 */
class RamAggregate : public RamSearch {
public:
    /** Types of aggregation functions */
    enum Function { MAX, MIN, COUNT, SUM };

    RamAggregate(std::unique_ptr<RamOperation> nested, Function fun,
            std::unique_ptr<RamExpression> expression, std::unique_ptr<RamRelationReference> relRef,
            size_t ident)
            : RamSearch(RN_Aggregate, ident, std::move(nested)), function(fun),
              expression(std::move(expression)), relationRef(std::move(relRef)),
              pattern(getRelation().getArity()) {}

    /** Get condition */
    RamCondition* getCondition() const {
        return condition.get();
    }

    /** Get aggregation function */
    Function getFunction() const {
        return function;
    }

    /** Get target expression */
    const RamExpression* getExpression() const {
        assert(expression);
        return expression.get();
    }

    /** Get relation */
    const RamRelation& getRelation() const {
        return *relationRef->get();
    }

    /** Get pattern */
    std::vector<RamExpression*> getPattern() const {
        return toPtrVector(pattern);
    }

    /** Get range query columns */
    SearchColumns getRangeQueryColumns() const {
        return keys;
    }

    /** Get indexable element */
    std::unique_ptr<RamExpression> getIndexElement(RamCondition* c, size_t& element, size_t level);

    /** Add condition */
    void addCondition(std::unique_ptr<RamCondition> newCondition);

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times(" ", tabpos);
        switch (function) {
            case MIN:
                os << "MIN ";
                break;
            case MAX:
                os << "MAX ";
                break;
            case COUNT:
                os << "COUNT ";
                break;
            case SUM:
                os << "SUM ";
                break;
        }
        if (function != COUNT) {
            os << *expression << " ";
        }
        os << "AS t" << getIdentifier() << ".0 IN t" << getIdentifier() << " ∈ " << getRelation().getName();
        os << "("
           << join(pattern, ",",
                      [&](std::ostream& out, const std::unique_ptr<RamExpression>& expression) {
                          if (expression == nullptr) {
                              out << "_";
                          } else {
                              out << *expression;
                          }
                      })
           << ")";
        if (condition != nullptr) {
            os << " WHERE " << getCondition();
        }
        os << std::endl;
        RamSearch::print(os, tabpos + 1);
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        auto res = RamSearch::getChildNodes();
        if (condition != nullptr) {
            res.push_back(condition.get());
        }
        for (auto& cur : pattern) {
            if (cur != nullptr) {
                res.push_back(cur.get());
            }
        }
        return res;
    }

    /** Create clone */
    RamAggregate* clone() const override {
        RamAggregate* res = new RamAggregate(std::unique_ptr<RamOperation>(getOperation().clone()), function,
                expression == nullptr ? nullptr : std::unique_ptr<RamExpression>(expression->clone()),
                std::unique_ptr<RamRelationReference>(relationRef->clone()), getIdentifier());
        res->keys = keys;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] != nullptr) {
                res->pattern[i] = std::unique_ptr<RamExpression>(pattern[i]->clone());
            }
        }
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        RamSearch::apply(map);
        if (condition != nullptr) {
            condition = map(std::move(condition));
        }
        relationRef = map(std::move(relationRef));
        if (expression != nullptr) {
            expression = map(std::move(expression));
        }
        for (auto& cur : pattern) {
            if (cur != nullptr) {
                cur = map(std::move(cur));
            }
        }
    }

protected:
    /**
     * Condition that is checked for each obtained tuple
     *
     * If condition is a nullptr, then no condition applies
     */
    std::unique_ptr<RamCondition> condition;

    /** Aggregation function */
    Function function;

    /** Aggregation expression */
    std::unique_ptr<RamExpression> expression;

    /** Aggregation relation */
    std::unique_ptr<RamRelationReference> relationRef;

    /** Pattern for filtering tuples */
    std::vector<std::unique_ptr<RamExpression>> pattern;

    /** Columns to be matched when using a range query */
    SearchColumns keys = 0;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamAggregate*>(&node));
        const auto& other = static_cast<const RamAggregate&>(node);
        if (getCondition() != nullptr && other.getCondition() != nullptr &&
                *getCondition() != *other.getCondition()) {
            return false;
        }
        return RamSearch::equal(other) && getCondition() == other.getCondition() &&
               getFunction() == other.getFunction() && getRelation() == other.getRelation() &&
               getExpression() == other.getExpression() && equal_targets(pattern, other.pattern) &&
               getRangeQueryColumns() == other.getRangeQueryColumns();
    }
};

/**
 * Filter statement
 */
class RamFilter : public RamNestedOperation {
public:
    RamFilter(std::unique_ptr<RamCondition> cond, std::unique_ptr<RamOperation> nested,
            std::string profileText = "")
            : RamNestedOperation(RN_Filter, std::move(nested), std::move(profileText)),
              condition(std::move(cond)) {}

    /** Get condition */
    const RamCondition& getCondition() const {
        return *condition;
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times(" ", tabpos);
        os << "IF " << getCondition() << std::endl;
        RamNestedOperation::print(os, tabpos + 1);
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {condition.get(), &getOperation()};
    }

    /** Create clone */
    RamFilter* clone() const override {
        return new RamFilter(std::unique_ptr<RamCondition>(condition->clone()),
                std::unique_ptr<RamOperation>(getOperation().clone()));
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        RamNestedOperation::apply(map);
        condition = map(std::move(condition));
    }

protected:
    /**
     * Condition that is checked for each obtained tuple
     *
     * If condition is a nullptr, then no condition applies
     */
    std::unique_ptr<RamCondition> condition;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamFilter*>(&node));
        const auto& other = static_cast<const RamFilter&>(node);
        return RamNestedOperation::equal(node) && getCondition() == other.getCondition();
    }
};

/** Projection */
class RamProject : public RamOperation {
public:
    RamProject(std::unique_ptr<RamRelationReference> relRef,
            std::vector<std::unique_ptr<RamExpression>> expressions)
            : RamOperation(RN_Project), relationRef(std::move(relRef)), expressions(std::move(expressions)) {}

    /** Get relation */
    const RamRelation& getRelation() const {
        return *relationRef->get();
    }

    /** Get expressions */
    std::vector<RamExpression*> getValues() const {
        return toPtrVector(expressions);
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times(" ", tabpos);
        os << "PROJECT (" << join(expressions, ", ", print_deref<std::unique_ptr<RamExpression>>())
           << ") INTO " << getRelation().getName() << std::endl;
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        std::vector<const RamNode*> res;
        res.push_back(relationRef.get());
        for (const auto& cur : expressions) {
            res.push_back(cur.get());
        }
        return res;
    }

    /** Create clone */
    RamProject* clone() const override {
        std::vector<std::unique_ptr<RamExpression>> newValues;
        for (auto& cur : expressions) {
            newValues.emplace_back(cur->clone());
        }
        RamProject* res = new RamProject(
                std::unique_ptr<RamRelationReference>(relationRef->clone()), std::move(newValues));
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        relationRef = map(std::move(relationRef));
        for (auto& cur : expressions) {
            cur = map(std::move(cur));
        }
    }

protected:
    /** Relation */
    std::unique_ptr<RamRelationReference> relationRef;

    /* Values for projection */
    std::vector<std::unique_ptr<RamExpression>> expressions;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamProject*>(&node));
        const auto& other = static_cast<const RamProject&>(node);
        return getRelation() == other.getRelation() && equal_targets(expressions, other.expressions);
    }
};

/** A statement for returning from a ram subroutine */
class RamReturn : public RamOperation {
public:
    RamReturn(std::vector<std::unique_ptr<RamExpression>> vals)
            : RamOperation(RN_Return), expressions(std::move(vals)) {}

    void print(std::ostream& os, int tabpos) const override {
        os << times(" ", tabpos);
        os << "RETURN (";
        for (auto val : getValues()) {
            if (val == nullptr) {
                os << "_";
            } else {
                val->print(os);
            }

            if (val != *(getValues().end() - 1)) {
                os << ", ";
            }
        }
        os << ")" << std::endl;
    }

    std::vector<RamExpression*> getValues() const {
        return toPtrVector(expressions);
    }

    /** Get expression */
    RamExpression& getValue(size_t i) const {
        assert(i < expressions.size() && "expression index out of range");
        return *expressions[i];
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        std::vector<const RamNode*> res;
        for (const auto& cur : expressions) {
            res.push_back(cur.get());
        }
        return res;
    }

    /** Create clone */
    RamReturn* clone() const override {
        std::vector<std::unique_ptr<RamExpression>> newValues;
        for (auto& cur : expressions) {
            newValues.emplace_back(cur->clone());
        }
        return new RamReturn(std::move(newValues));
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        for (auto& cur : expressions) {
            cur = map(std::move(cur));
        }
    }

protected:
    std::vector<std::unique_ptr<RamExpression>> expressions;

    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamReturn*>(&node));
        const auto& other = static_cast<const RamReturn&>(node);
        return equal_targets(expressions, other.expressions);
    }
};

}  // namespace souffle
