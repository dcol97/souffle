/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2019, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RamIndexScanKeys.cpp
 *
 * Implementation of RamIndexScan Keys Analysis
 *
 ***********************************************************************/

#include "RamIndexScanKeys.h"
#include "RamVisitor.h"

namespace souffle {

/** Get indexable columns of index scan */
SearchColumns RamIndexScanKeysAnalysis::getRangeQueryColumns(const RamIndexScan* scan) const {
    return getRangeQueryColumnsHelper(scan->getRangePattern());
}

/** Get indexable columns of index choice */
SearchColumns RamIndexScanKeysAnalysis::getRangeQueryColumns(const RamIndexChoice* choice) const {
    return getRangeQueryColumnsHelper(choice->getRangePattern());
}

SearchColumns RamIndexScanKeysAnalysis::getRangeQueryColumnsHelper(
        const std::vector<RamExpression*> rangePattern) const {
    SearchColumns keys = 0;
    for (std::size_t i = 0; i < rangePattern.size(); i++) {
        if (rangePattern[i] != nullptr) {
            keys |= (1 << i);
        }
    }
    return keys;
}

}  // end of namespace souffle
