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

#include "Macro.h"
#include "RamCondition.h"
#include "RamNode.h"
#include "RamRelation.h"
#include "RamTypes.h"
#include "RamValue.h"
#include "Util.h"
#include <cassert>
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
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

    /** Create clone */
    RamOperation* clone() const override = 0;
};

/**
 * Abstract class for nesting operations 
 */
class RamNestedOperation : public RamOperation {
    /** Nested operation */
    std::unique_ptr<RamOperation> nestedOperation;

public:
    RamNestedOperation(RamNodeType type, std::unique_ptr<RamOperation> nested)
            : RamOperation(type), nestedOperation(std::move(nested)) {}

    /** Print */ 
    void print(std::ostream &os, int tabpos) const override { 
        nestedOperation->print(os, tabpos + 1);
    } 

    /** get nested operation */
    const RamOperation& getOperation() const {
        ASSERT(nestedOperation);
        return *nestedOperation;
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {nestedOperation.get()};
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        RamOperation::apply(map);
        nestedOperation = map(std::move(nestedOperation));
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamNestedOperation*>(&node));
        const auto& other = static_cast<const RamNestedOperation&>(node);
        return getOperation() == other.getOperation();
    }
};

/**
 * RamScan: enumerate all tuples in a relation
 */
class RamScan : public RamNestedOperation {
protected:

    /** Search relation */
    std::unique_ptr<RamRelation> relation;

    /** identifier for the tuple */
    size_t identifier; 

public:
    RamScan(std::unique_ptr<RamRelation> r, size_t ident, std::unique_ptr<RamOperation> nested)
            : RamNestedOperation(RN_Scan, std::move(nested)), identifier(ident), relation(std::move(r)) {}

    /** Get search relation */
    const RamRelation& getRelation() const {
        return *relation;
    }

    /** Get identifier */ 
    const size_t getIdentifier() const {
        return identifier;
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times('\t', tabpos) << "for t" << identifier << " in " << getRelation().getName() << "{\n";
        RamNestedOperation::print(os, tabpos+1);
        os << times('\t', tabpos) << "}\n";
    } 

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {&getOperation(), relation.get()};
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        RamNestedOperation::apply(map);
        relation = map(std::move(relation));
    }

    /** Create clone */
    RamScan* clone() const override {
        RamScan* res = new RamScan(std::unique_ptr<RamRelation>(relation->clone()), identifier,
                                   std::unique_ptr<RamOperation>(getOperation().clone()));
        return res;
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamScan*>(&node));
        const auto& other = static_cast<const RamScan&>(node);
        return RamNestedOperation::equal(other) && 
               identifier == other.getIdentifier() &&
               getRelation() == other.getRelation();
    }
};

/**
 * Record lookup
 */
class RamLookup : public RamNestedOperation {
    /** Reference value */
    std::unique_ptr<RamValue> value;

    /** Arity of the unpacked tuple */
    std::size_t arity;

public:
    RamLookup(std::unique_ptr<RamOperation> nested, std::unique_ptr<RamValue> val, size_t arity) 
            : RamNestedOperation(RN_Lookup, std::move(nested)), value(std::move(val)), arity(arity) {}

    /** Get value */ 
    RamValue &getValue() const {
       ASSERT(value != nullptr && "value is a nullptr");
       return *value;
    }

    /** Get Arity */
    std::size_t getArity() const {
        return arity;
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times('\t', tabpos) << "RECORD LOOKUP (";
        // os << value->print(os);
        os  << "," << arity << ")\n";
        RamNestedOperation::print(os, tabpos + 1);
        os << times('\t', tabpos) << "}\n";
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {&getOperation(), value.get()};
    }

    /** Create clone */
    RamLookup* clone() const override {
        RamLookup* res = new RamLookup(
                    std::unique_ptr<RamOperation>(getOperation().clone()), 
                    std::unique_ptr<RamValue>(value->clone()), 
                        arity);
        return res;
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamLookup*>(&node));
        const auto& other = static_cast<const RamLookup&>(node);
        return RamNestedOperation::equal(other) && 
               getValue() == other.getValue() && 
               getArity() == other.getArity();
    }
};

/**
 * Aggregation
 */
class RamAggregate : public RamNestedOperation {
public:
    /** Types of aggregation functions */
    enum Function { MAX, MIN, COUNT, SUM };

private:
    /** Aggregation function */
    Function fun;

    /** Aggregate operation */ 
    std::unique_ptr<RamOperation> aggOperation;

public:
    RamAggregate(std::unique_ptr<RamOperation> nested, Function fun, std::unique_ptr<RamOperation> op)
            : RamNestedOperation(RN_Aggregate, std::move(nested)), 
              fun(fun),
              aggOperation(std::move(op)) { }

    /** Get aggregation function */
    Function getFunction() const {
        return fun;
    }

    /** Get aggregate operation */ 
    RamOperation &getOperation() const {
        ASSERT(aggOperation != nullptr); 
        return *aggOperation;
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times('\t', tabpos) << "AGGREGATE ";
        switch(fun) {
        case MIN: 
           os << "MIN ";
           break;
        case MAX: 
           os << "MAX ";
           break;
        case SUM: 
           os << "SUM ";
           break;
        case COUNT: 
           os << "COUNT ";
           break;
        default:
           abort(); 
        }
        os << "{\n";
        RamNestedOperation::print(os,tabpos+1);
        os << times('\t', tabpos) << "}\n";
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {&getOperation(), aggOperation.get()};
    }

    /** Create clone */
    RamAggregate* clone() const override {
        RamAggregate* res = new RamAggregate(std::unique_ptr<RamOperation>(getOperation().clone()),
                fun, std::unique_ptr<RamOperation>(aggOperation->clone()));
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        RamNestedOperation::apply(map);
        aggOperation = map(std::move(aggOperation));
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamAggregate*>(&node));
        const auto& other = static_cast<const RamAggregate&>(node);
        return RamNestedOperation::equal(other) && 
               getFunction() == other.getFunction() &&
               getOperation() == other.getOperation();
    }
};

/**
 * Filter statement 
 */
class RamFilter : public RamNestedOperation {
protected:
    /** Relation */
    std::unique_ptr<RamCondition> condition; 

public:
    RamFilter(std::unique_ptr<RamCondition> cond, std::unique_ptr<RamOperation> nested)
              : RamNestedOperation(RN_Filter, std::move(nested)), condition(std::move(cond)) {}

    /** Get relation */
    const RamCondition& getCondition() const {
        return *condition;
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times('\t', tabpos) << "IF ";
        getCondition().print(os);
        os << "{\n";
        RamNestedOperation::print(os, tabpos+1);
        os << times('\t', tabpos) << "}\n";
    } 

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {&getOperation(), condition.get()};
    }

    /** Create clone */
    RamFilter* clone() const override {
        RamFilter* res = new RamFilter(std::unique_ptr<RamCondition>(condition->clone()),
                                       std::unique_ptr<RamOperation>(getOperation().clone()));
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
	RamNestedOperation::apply(map);
        condition = map(std::move(condition));
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamFilter*>(&node));
        const auto& other = static_cast<const RamFilter&>(node);
        return getCondition() == other.getCondition() &&
               RamNestedOperation::equal(node);
    }
};

/**
 * Projection 
 */
class RamProject : public RamOperation {
protected:
    /** Relation */
    std::unique_ptr<RamRelation> relation;

    /* Values for projection */
    std::vector<std::unique_ptr<RamValue>> values;

public:
    RamProject(std::unique_ptr<RamRelation> rel, std::vector<std::unique_ptr<RamValue>> vals)
            : RamOperation(RN_Project), relation(std::move(rel)), values(std::move(vals)) { }

    /** Get relation */
    const RamRelation& getRelation() const {
        return *relation;
    }

    /** Get values */
    std::vector<RamValue*> getValues() const {
        return toPtrVector(values);
    }

    /** Print */
    void print(std::ostream& os, int tabpos) const override {
        os << times('\t', tabpos) << "PROJECT (";
        os << join(values, ",", [](std::ostream& out, const std::unique_ptr<RamValue>& arg) { arg->print(out); });
        os << ") INTO " << relation->getName() << "\n";
    } 

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        std::vector<const RamNode*> res = {relation.get()};
        for (const auto& cur : values) {
            res.push_back(cur.get());
        }
        return res;
    }

    /** Create clone */
    RamProject* clone() const override {
        RamProject* res = new RamProject(std::unique_ptr<RamRelation>(relation->clone()), {});
        for (auto& cur : values) {
            RamValue* val = cur->clone();
            res->values.push_back(std::unique_ptr<RamValue>(val));
        }
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        relation = map(std::move(relation));
        for (auto& cur : values) {
            cur = map(std::move(cur));
        }
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamProject*>(&node));
        const auto& other = static_cast<const RamProject&>(node);
        return getRelation() == other.getRelation() &&
               equal_targets(values, other.values);
    }
};

/** A statement for returning from a ram subroutine */
class RamReturn : public RamOperation {
protected:
    std::vector<std::unique_ptr<RamValue>> values;

public:
    RamReturn(std::vector<std::unique_ptr<RamValue>> val): RamOperation(RN_Return), values(std::move(val)) {}

    /** Print */ 
    void print(std::ostream& os, int tabpos) const override {
        os << times('\t', tabpos) << "RETURN (";
        os << join(values, ",", [](std::ostream& out, const std::unique_ptr<RamValue>& arg) { arg->print(out); });
        os << ")\n";
    } 

    /** Get values */ 
    std::vector<RamValue*> getValues() const {
        return toPtrVector(values);
    }

    /** Get value */
    RamValue& getValue(size_t i) const {
        assert(i < values.size() && "value index out of range");
        return *values[i];
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        std::vector<const RamNode*> res;
        for (const auto& cur : values) {
            res.push_back(cur.get());
        }
        return res;
    }

    /** Create clone */
    RamReturn* clone() const override {
        RamReturn* res = new RamReturn({});
        for (auto& cur : values) {
            RamValue* val = cur->clone();
            res->values.push_back(std::unique_ptr<RamValue>(val));
        }
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        for (auto& cur : values) {
            cur = map(std::move(cur));
        }
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamReturn*>(&node));
        const auto& other = static_cast<const RamReturn&>(node);
        return equal_targets(values, other.values);
    }
};

}  // namespace souffle
