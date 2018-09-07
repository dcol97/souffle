/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2018, The Souffle Developers. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file AstTranslator.cpp
 *
 * Implementations of a translator from AST to RAM structures.
 *
 ***********************************************************************/

#include "Global.h"
#include "AstTranslator.h"
#include <iostream>
#include <fstream>

namespace souffle {

std::unique_ptr<RamValue> translateValue(const AstArgument* arg, const ValueIndex& index = ValueIndex()) {
    class ValueTranslator : public AstVisitor<std::unique_ptr<RamValue>, const ValueIndex&> {
    public:
        ValueTranslator() = default;

        std::unique_ptr<RamValue> visitVariable(const AstVariable& var, const ValueIndex& index) override {
            ASSERT(index.isDefined(var) && "variable not grounded");
            const Location& loc = index.getDefinitionPoint(var);
            return std::make_unique<RamElementAccess>(loc.level, loc.component, loc.name);
        }

        std::unique_ptr<RamValue> visitUnnamedVariable(const AstUnnamedVariable& var, const ValueIndex& index) override {
            return nullptr;  // utilised to identify _ values
        }

        std::unique_ptr<RamValue> visitConstant(const AstConstant& c, const ValueIndex& index) override {
            return std::make_unique<RamNumber>(c.getIndex());
        }

        /* TODO: Visitors for n-ary functors, see **RamIntrinsic**.
        std::unique_ptr<RamValue> visitUnaryFunctor(const AstUnaryFunctor& uf, const ValueIndex& index) override {
            return std::make_unique<RamUnaryOperator>(uf.getFunction(), translateValue(uf.getOperand(), index));
        }

        std::unique_ptr<RamValue> visitBinaryFunctor(const AstBinaryFunctor& bf, const ValueIndex& index) override {
            return std::make_unique<RamBinaryOperator>(
                bf.getFunction(), translateValue(bf.getLHS(), index), translateValue(bf.getRHS(), index));
        }

        std::unique_ptr<RamValue> visitTernaryFunctor(const AstTernaryFunctor& tf, const ValueIndex& index) override {
            return std::make_unique<RamTernaryOperator>(tf.getFunction(), translateValue(tf.getArg(0), index),
                translateValue(tf.getArg(1), index), translateValue(tf.getArg(2), index));
        }

        std::unique_ptr<RamValue> visitCounter(const AstCounter& cnt, const ValueIndex& index) override {
            return std::make_unique<RamAutoIncrement>();
        }
        */

        std::unique_ptr<RamValue> visitRecordInit(const AstRecordInit& init, const ValueIndex& index) override {
            std::vector<std::unique_ptr<RamValue>> values;
            for (const auto& cur : init.getArguments()) {
                values.push_back(translateValue(cur, index));
            }
            return std::make_unique<RamPack>(std::move(values));
        }

        std::unique_ptr<RamValue> visitAggregator(const AstAggregator& agg, const ValueIndex& index) override {
            // here we look up the location the aggregation result gets bound
            auto loc = index.getAggregatorLocation(agg);
            return std::make_unique<RamElementAccess>(loc.level, loc.component, loc.name);
        }

        std::unique_ptr<RamValue> visitSubroutineArgument(const AstSubroutineArgument& subArg, const ValueIndex& index) override {
            return std::make_unique<RamArgument>(subArg.getNumber());
        }
    };

    return ValueTranslator()(*arg, index);
}

/** Translate AST to a RAM program  */
std::unique_ptr<RamProgram> AstTranslator::translateProgram(const AstTranslationUnit& translationUnit) {
    // obtain type environment from analysis
    const TypeEnvironment& typeEnv =
            translationUnit.getAnalysis<TypeEnvironmentAnalysis>()->getTypeEnvironment();

    // obtain recursive clauses from analysis
    const auto* recursiveClauses = translationUnit.getAnalysis<RecursiveClauses>();

    // obtain strongly connected component (SCC) graph from analysis
    const auto& sccGraph = *translationUnit.getAnalysis<SCCGraph>();

    // obtain some topological order over the nodes of the SCC graph
    const auto& sccOrder = translationUnit.getAnalysis<TopologicallySortedSCCGraph>()->order();

    // obtain the schedule of relations expired at each index of the topological order
    const auto& expirySchedule = translationUnit.getAnalysis<RelationSchedule>()->schedule();

    // start with an empty sequence of ram statements
    std::unique_ptr<RamStatement> res = std::make_unique<RamSequence>();

    // handle the case of an empty SCC graph
    if (sccGraph.getNumberOfSCCs() == 0) {
        return std::make_unique<RamProgram>(std::move(res));
    }

    // maintain the index of the SCC within the topological order
    unsigned index = 0;

    // iterate over each SCC according to the topological order
    for (const auto& scc : sccOrder) {
        // make a new ram statement for the current SCC
        std::unique_ptr<RamStatement> current;

        // find out if the current SCC is recursive
        const auto& isRecursive = sccGraph.isRecursive(scc);

        // make variables for particular sets of relations contained within the current SCC, and, predecessors
        // and successor SCCs thereof
        const auto& allInterns = sccGraph.getInternalRelations(scc);
        const auto& internIns = sccGraph.getInternalInputRelations(scc);
        const auto& internOuts = sccGraph.getInternalOutputRelations(scc);
        const auto& externOutPreds = sccGraph.getExternalOutputPredecessorRelations(scc);
        const auto& externNonOutPreds = sccGraph.getExternalNonOutputPredecessorRelations(scc);
        const auto& internNonOutsWithExternSuccs =
                sccGraph.getInternalNonOutputRelationsWithExternalSuccessors(scc);

        // make a variable for all relations that are expired at the current SCC
        const auto& internExps = expirySchedule.at(index).expired();

        // a function to create relations
        const auto& makeRamCreate = [&](const AstRelation* relation, const std::string relationNamePrefix) {
            appendStmt(current,
                    std::make_unique<RamCreate>(std::unique_ptr<RamRelation>(getRamRelation(relation,
                            &typeEnv, relationNamePrefix + getRelationName(relation->getName()),
                            relation->getArity(), !relationNamePrefix.empty(), relation->isHashset()))));
        };

        // a function to load relations
        const auto& makeRamLoad = [&](const AstRelation* relation, const std::string& inputDirectory,
                const std::string& fileExtension) {
            appendStmt(current,
                    std::make_unique<RamLoad>(std::unique_ptr<RamRelation>(getRamRelation(relation, &typeEnv,
                                                      getRelationName(relation->getName()),
                                                      relation->getArity(), false, relation->isHashset())),
                            getInputIODirectives(
                                    relation, Global::config().get(inputDirectory), fileExtension)));
        };

        // a function to print the size of relations
        const auto& makeRamPrintSize = [&](const AstRelation* relation) {
            appendStmt(current, std::make_unique<RamPrintSize>(std::unique_ptr<RamRelation>(getRamRelation(
                                        relation, &typeEnv, getRelationName(relation->getName()),
                                        relation->getArity(), false, relation->isHashset()))));
        };

        // a function to store relations
        const auto& makeRamStore = [&](const AstRelation* relation, const std::string& outputDirectory,
                const std::string& fileExtension) {
            appendStmt(current,
                    std::make_unique<RamStore>(std::unique_ptr<RamRelation>(getRamRelation(relation, &typeEnv,
                                                       getRelationName(relation->getName()),
                                                       relation->getArity(), false, relation->isHashset())),
                            getOutputIODirectives(relation, &typeEnv, Global::config().get(outputDirectory),
                                    fileExtension)));
        };

        // a function to drop relations
        const auto& makeRamDrop = [&](const AstRelation* relation) {
            appendStmt(current, std::make_unique<RamDrop>(getRamRelation(relation, &typeEnv,
                                        getRelationName(relation->getName()), relation->getArity(), false,
                                        relation->isHashset())));
        };

        // create all internal relations of the current scc
        for (const auto& relation : allInterns) {
            makeRamCreate(relation, "");
            // create new and delta relations if required
            if (isRecursive) {
                makeRamCreate(relation, "delta_");
                makeRamCreate(relation, "new_");
            }
        }

        // load all internal input relations from the facts dir with a .facts extension
        for (const auto& relation : internIns) {
            makeRamLoad(relation, "fact-dir", ".facts");
        }

        // if a communication engine has been specified...
        if (Global::config().has("engine")) {
            // load all external output predecessor relations from the output dir with a .csv extension
            for (const auto& relation : externOutPreds) {
                makeRamLoad(relation, "output-dir", ".csv");
            }
            // load all external output predecessor relations from the output dir with a .facts extension
            for (const auto& relation : externNonOutPreds) {
                makeRamLoad(relation, "output-dir", ".facts");
            }
        }

        // compute the relations themselves
        std::unique_ptr<RamStatement> bodyStatement =
                (!isRecursive) ? translateNonRecursiveRelation(*((const AstRelation*)*allInterns.begin()),
                                         translationUnit.getProgram(), recursiveClauses, typeEnv)
                               : translateRecursiveRelation(
                                         allInterns, translationUnit.getProgram(), recursiveClauses, typeEnv);
        appendStmt(current, std::move(bodyStatement));

        // print the size of all printsize relations in the current SCC
        for (const auto& relation : allInterns) {
            if (relation->isPrintSize()) {
                makeRamPrintSize(relation);
            }
        }

        // if a communication engine is enabled...
        if (Global::config().has("engine")) {
            // store all internal non-output relations with external successors to the output dir with a
            // .facts extension
            for (const auto& relation : internNonOutsWithExternSuccs) {
                makeRamStore(relation, "output-dir", ".facts");
            }
        }

        // store all internal output relations to the output dir with a .csv extension
        for (const auto& relation : internOuts) {
            makeRamStore(relation, "output-dir", ".csv");
        }

        // if provenance is not enabled...
        if (!Global::config().has("provenance")) {
            // if a communication engine is enabled...
            if (Global::config().has("engine")) {
                // drop all internal relations
                for (const auto& relation : allInterns) {
                    makeRamDrop(relation);
                }
                // drop external output predecessor relations
                for (const auto& relation : externOutPreds) {
                    makeRamDrop(relation);
                }
                // drop external non-output predecessor relations
                for (const auto& relation : externNonOutPreds) {
                    makeRamDrop(relation);
                }
            } else {
                // otherwise, drop all  relations expired as per the topological order
                for (const auto& relation : internExps) {
                    makeRamDrop(relation);
                }
            }
        }

        if (current) {
            // append the current SCC as a stratum to the sequence
            appendStmt(res, std::make_unique<RamStratum>(std::move(current), index));
            // increment the index of the current SCC
            index++;
        }
    }

    // add main timer if profiling
    if (res && Global::config().has("profile")) {
        res = std::make_unique<RamLogTimer>(std::move(res), LogStatement::runtime());
    }

    // done for main prog
    std::unique_ptr<RamProgram> prog(new RamProgram(std::move(res)));

    // add subroutines for each clause
    if (Global::config().has("provenance")) {
        visitDepthFirst(translationUnit.getProgram()->getRelations(), [&](const AstClause& clause) {
            std::stringstream relName;
            relName << clause.getHead()->getName();

            if (relName.str().find("@info") != std::string::npos || clause.getBodyLiterals().empty()) {
                return;
            }

            std::string subroutineLabel =
                    relName.str() + "_" + std::to_string(clause.getClauseNum()) + "_subproof";
            prog->addSubroutine(
                    subroutineLabel, makeSubproofSubroutine(clause, translationUnit.getProgram(), typeEnv));
        });
    }

    return prog;
}

std::unique_ptr<RamTranslationUnit> AstTranslator::translateUnit(AstTranslationUnit& tu) {

    auto translateStart = std::chrono::high_resolution_clock::now();

    std::unique_ptr<RamProgram> ramProg = nullptr;

    SymbolTable& symTab = tu.getSymbolTable();
    ErrorReport& errReport = tu.getErrorReport();
    DebugReport& debugReport = tu.getDebugReport();

    if (!Global::config().get("debug-report").empty()) {
        if (ramProg) {
            auto translateEnd = std::chrono::high_resolution_clock::now();
            std::string runtimeStr =
                    "(" + std::to_string(std::chrono::duration<double>(translateEnd - translateStart).count()) + "s)";
            std::stringstream ramProgStr;
            ramProgStr << *ramProg;
            debugReport.addSection(DebugReporter::getCodeSection(
                    "ram-program", "RAM Program " + runtimeStr, ramProgStr.str()));
        }

        if (!debugReport.empty()) {
            std::ofstream debugReportStream(Global::config().get("debug-report"));
            debugReportStream << debugReport;
        }
    }
    return std::make_unique<RamTranslationUnit>(std::move(ramProg), symTab, errReport, debugReport);
   return nullptr;
}

}
