/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RamValue.h
 *
 * Defines a class for evaluating values in the Relational Algebra Machine
 *
 ************************************************************************/

#pragma once

#include "BinaryFunctorOps.h"
#include "RamNode.h"
#include "RamRelation.h"
#include "SymbolTable.h"
#include "TernaryFunctorOps.h"
#include "UnaryFunctorOps.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>

#include <cstdlib>
#include <utility>

namespace souffle {

/**
 * RAM Value class
 */
class RamValue : public RamNode {
public:
    RamValue(RamNodeType type) : RamNode(type) {}

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {};
    }

    /** Create clone */
    RamValue* clone() const override = 0;

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return {};
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
    }
};

/**
 *  RAM Operator
 * 
 *  Assumption that all arguments are specified (not equal to nullptr) 
 */
class RamOperator : public RamValue {
private:
    /** Operation symbol */
    RamOp operation;

    /** Argument of unary function */
    std::vector<std::unique_ptr<RamValue>> arguments;  

public:
    RamOperator(RamOp op, std::vector<std::unique_ptr<RamValue>> args)
            : RamValue(RN_Operator), operation(op), arguments(std::mov(args)) {}

    /** Print */
    void print(std::ostream& os) const override {
        os << getSymbolForOp(operation) << "(";
        os << join(arguments, ",", [](std::ostream& out, const std::unique_ptr<RamValue>& arg) { arg->print(out); });
        os << ")";
    }

    /** Get operator */
    RamOp getOperator() const {
        return operation;
    }

    /** Get arguments */ 
    std::vector<RamValue*> getArguments() const {
        return toPtrVector(arguments);
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        std::vector<const RamNode *> res; 
        for(const auto &cur: values) {
           res.push_back(cur);
        } 
        return res;
    }

    /** Create clone */
    RamPack* clone() const override {
        RamPack* res = new RamPack({});
        for (auto& cur : arguments) {
            RamValue* arg = cur->clone();
            res->arguments.push_back(std::unique_ptr<RamValue>(arg));
        }
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        for (auto& arg : arguments) {
            arg = map(std::move(arg));
        }
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamPack*>(&node));
        const auto& other = static_cast<const RamPack&>(node);
        return getOperator() == other.getOperator() && equal_targets(arguments, other.arguments);
    }
};


/**
 * Access element from the current tuple in a tuple environment
 */
class RamElementAccess : public RamValue {
private:
    /** Level information */
    size_t ident;

    /** Element number */
    size_t element;

    /** Name of attribute (optional) */
    std::string name;

public:
    RamElementAccess(size_t i, size_t e, std::string n = "")
            : RamValue(RN_ElementAccess), ident(i), element(e), name(n) {}

    /** Print */
    void print(std::ostream& os) const override {
        if (name.empty()) {
            os << "env(t" << ident << ", i" << element << ")";
        } else {
            os << "t" << ident << "." << name;
        }
    }

    /** Get level */
    size_t getIdent() const {
        return ident;
    }

    /** Get element */
    size_t getElement() const {
        return element;
    }

    /** Get name */
    const std::string& getName() const {
        return name;
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return std::vector<const RamNode*>();  // no child nodes
    }

    /** Create clone */
    RamElementAccess* clone() const override {
        RamElementAccess* res = new RamElementAccess(ident, element, name);
        return res;
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamElementAccess*>(&node));
        const RamElementAccess& other = static_cast<const RamElementAccess&>(node);
        return getLevel() == other.getLevel() && getElement() == other.getElement() &&
               getName() == other.getName();
    }
};

/**
 * Number Constant
 */
class RamNumber : public RamValue {

    /** Constant value */
    RamDomain constant;

public:
    RamNumber(RamDomain c) : RamValue(RN_Number, true), constant(c) {}

    /** Get constant */
    RamDomain getConstant() const {
        return constant;
    }

    /** Print */
    void print(std::ostream& os) const override {
        os << "number(" << constant << ")";
    }

    /** Create clone */
    RamNumber* clone() const override {
        auto* res = new RamNumber(constant);
        return res;
    }


protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamNumber*>(&node));
        const auto& other = static_cast<const RamNumber&>(node);
        return getConstant() == other.getConstant();
    }
};

/**
 * Record pack operation
 */
class RamPack : public RamValue {
private:
    /** Arguments */
    std::vector<std::unique_ptr<RamValue>> arguments;

public:
    RamPack(std::vector<std::unique_ptr<RamValue>> args)
            : RamValue(RN_Pack), arguments(std::move(args)) {}


    /** Get arguments */ 
    std::vector<RamValue*> getArguments() const {
        return toPtrVector(arguments);
    }

    /** Print */
    void print(std::ostream& os) const override {
        os << "[" << join(arguments, ",", [](std::ostream& out, const std::unique_ptr<RamValue>& arg) {
            if (arg) {
                out << *arg;
            } else {
                out << "_";
            }
        }) << "]";
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        std::vector<const RamNode*> res;
        for (const auto& cur : arguments) {
            if (cur) {
                res.push_back(cur.get());
            }
        }
        return res;
    }

    /** Create clone */
    RamPack* clone() const override {
        RamPack* res = new RamPack({});
        for (auto& cur : arguments) {
            RamValue* arg = nullptr;
            if (cur != nullptr) {
                arg = cur->clone();
            }
            res->arguments.push_back(std::unique_ptr<RamValue>(arg));
        }
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        for (auto& arg : arguments) {
            if (arg != nullptr) {
                arg = map(std::move(arg));
            }
        }
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamPack*>(&node));
        const auto& other = static_cast<const RamPack&>(node);
        return equal_targets(arguments, other.arguments);
    }
};

/**
 * Access argument of a subroutine
 *
 * Arguments are number from zero 0 to n-1
 * where n is the number of arguments of the
 * subroutine.
 */
class RamArgument : public RamValue {
    /** Argument number */
    size_t number;

public:
    RamArgument(size_t number) : RamValue(RN_Argument), number(number) {}

    /** Get argument number */
    size_t getArgNumber() const {
        return number;
    }

    /** Print */
    void print(std::ostream& os) const override {
        os << "arg(" << number << ")";
    }

    /** Obtain list of child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        return std::vector<const RamNode*>();
    }

    /** Create clone */
    RamArgument* clone() const override {
        auto* res = new RamArgument(getArgNumber());
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {}

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamArgument*>(&node));
        const auto& other = static_cast<const RamArgument&>(node);
        return getArgNumber() == other.getArgNumber();
    }
};

}  // end of namespace souffle
