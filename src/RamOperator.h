/*
 * Souffle - A Datalog Compiler
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RamOperator.h
 *
 * Defines RAM operator
 *
 ***********************************************************************/

#pragma once

#include <cassert>

namespace souffle {

/**
 * Ram Operator codes
 */
enum class RamOperator {
   __UNDEFINED__,

    // UNARY
    ORD,     // ordinal number of a string
    STRLEN,  // length of a string
    NEG,     // numeric negation
    BNOT,    // bitwise negation
    LNOT,    // logical negation

    // BINARY
    ADD,            // addition
    SUB,            // subtraction
    MUL,            // multiplication
    DIV,            // division
    EXP,            // exponent
    MOD,            // modulus
    BAND,           // bitwise and
    BOR,            // bitwise or
    BXOR,           // bitwise exclusive or
    LAND,           // logical and
    LOR,            // logical or
    MAX,            // max of two numbers
    MIN,            // min of two numbers
    CAT,            // string concatenation

    // TERNARY
    SUBSTR,         // addition
};

/**
 * Returns the corresponding symbol for a RAM operator code
 */
inline const std::string getRamOpSymbol(RamOperator op) {
    switch (op) {

        // UNARY
        case RamOperator::ORD:
            return "ord";
        case RamOperator::STRLEN:
            return "strlen";
        case RamOperator::NEG:
            return "-";
        case RamOperator::BNOT:
            return "bnot";
        case RamOperator::LNOT:
            return "lnot";

        // Binary
        case RamOperator::ADD:
            return "+";
        case RamOperator::SUB:
            return "-";
        case RamOperator::MUL:
            return "*";
        case RamOperator::DIV:
            return "/";
        case RamOperator::EXP:
            return "^";
        case RamOperator::MOD:
            return "%";
        case RamOperator::BAND:
            return "band";
        case RamOperator::BOR:
            return "bor";
        case RamOperator::BXOR:
            return "bxor";
        case RamOperator::LAND:
            return "land";
        case RamOperator::LOR:
            return "lor";
        case RamOperator::MAX:
            return "max";
        case RamOperator::MIN:
            return "min";
        case RamOperator::CAT:
            return "cat";

        // Ternary
        case RamOperator::SUBSTR:
            return "substr";

        default:
            break;
    }
    assert(false && "Unsupported RamOperator!");
    return "?";
}



}  // end of namespace souffle
