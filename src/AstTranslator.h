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
    /** Translate to RAM program */
    std::unique_ptr<RamProgram> translateProgram(const AstTranslationUnit& translationUnit);
};

}  // end of namespace souffle
