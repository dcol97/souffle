/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2018, The Souffle Developers. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file AstTranslator.h
 *
 * Defines utilities for translating AST structures into RAM constructs.
 *
 ***********************************************************************/

#pragma once

#include "AstTranslationUnit.h"
#include "RamTranslationUnit.h"
#include "PrecedenceGraph.h"
#include "AstTypeAnalysis.h"
#include <map>
#include <memory>
#include <set>
#include <string>

namespace souffle {

/**
 * A utility class capable of conducting the conversion between AST
 * and RAM structures.
 */
class AstTranslator {
public:
    /** translates AST to RAM translation unit */
    std::unique_ptr<RamTranslationUnit> translateUnit(AstTranslationUnit& tu);

private:
    /** translate an AstValue to RamValue */
    std::unique_ptr<RamValue> translateValue(const AstArgument* arg);

    /** translate to RAM program */
    std::unique_ptr<RamProgram> translateProgram(const AstTranslationUnit& translationUnit);

    /** translate non-recursive rules */
    std::unique_ptr<RamStatement> translateNonRecursiveRelation(const AstRelation& rel,
        const AstProgram* program, const RecursiveClauses* recursiveClauses, const TypeEnvironment& typeEnv);

    /** translate recursive rules */
    std::unique_ptr<RamStatement> translateRecursiveRelation(
        const std::set<const AstRelation*>& scc, const AstProgram* program,
        const RecursiveClauses* recursiveClauses, const TypeEnvironment& typeEnv);

    std::unique_ptr<RamStatement> translateClause(const AstClause& clause,
        const AstProgram* program, const TypeEnvironment* typeEnv);

};

}  // end of namespace souffle
