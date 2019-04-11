/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2018, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RamTransforms.cpp
 *
 * Implementation of RAM transformation passes.
 *
 ***********************************************************************/

#include "RamTransforms.h"
#include "BinaryConstraintOps.h"
#include "RamCondition.h"
#include "RamExpression.h"
#include "RamNode.h"
#include "RamOperation.h"
#include "RamProgram.h"
#include "RamRelation.h"
#include "RamStatement.h"
#include "RamTypes.h"
#include "RamVisitor.h"
#include <utility>
#include <vector>

namespace souffle {

namespace {

std::vector<std::unique_ptr<RamCondition>> getConditions(const RamCondition* condition) {
    std::vector<std::unique_ptr<RamCondition>> conditions;
    while (condition != nullptr) {
        if (const auto* ramConj = dynamic_cast<const RamConjunction*>(condition)) {
            conditions.emplace_back(ramConj->getRHS().clone());
            condition = &ramConj->getLHS();
        } else {
            conditions.emplace_back(condition->clone());
            break;
        }
    }
    return conditions;
}

}  // namespace

bool LevelConditionsTransformer::levelConditions(RamProgram& program) {
    // Node-mapper that collects nested conditions which apply to a given scan level
    // TODO: Change these to LambdaRamNodeMapper lambdas
    class RamFilterCapturer : public RamNodeMapper {
        LevelConditionsTransformer* context;

        /** identifier for the tuple */
        const size_t identifier;

        mutable std::unique_ptr<RamCondition> condition;

    public:
        RamFilterCapturer(LevelConditionsTransformer* l, const size_t ident)
                : context(l), identifier(ident) {}

        std::unique_ptr<RamCondition> getCondition() const {
            return std::move(condition);
        }

        void addCondition(std::unique_ptr<RamCondition> c) const {
            if (condition != nullptr) {
                condition = std::make_unique<RamConjunction>(std::move(condition), std::move(c));
            } else {
                condition = std::move(c);
            }
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            if (auto* filter = dynamic_cast<RamFilter*>(node.get())) {
                const RamCondition& condition = filter->getCondition();

                if (context->rcla->getLevel(&condition) == identifier) {
                    addCondition(std::unique_ptr<RamCondition>(condition.clone()));

                    // skip this filter
                    node->apply(*this);
                    return std::unique_ptr<RamOperation>(filter->getOperation().clone());
                }
            }

            node->apply(*this);
            return node;
        }
    };

    class RamFilterInsert : public RamNodeMapper {
        std::unique_ptr<RamCondition> condition;

    public:
        RamFilterInsert(std::unique_ptr<RamCondition> c) : condition(std::move(c)) {}

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            if (nullptr != dynamic_cast<RamOperation*>(node.get())) {
                return std::make_unique<RamFilter>(std::unique_ptr<RamCondition>(condition->clone()),
                        std::unique_ptr<RamOperation>(dynamic_cast<RamOperation*>(node.release())));
            }
            return node;
        }
    };

    // Node-mapper that searches for and updates RAM scans nested in RAM inserts
    class RamScanCapturer : public RamNodeMapper {
        mutable bool modified = false;
        LevelConditionsTransformer* context;

    public:
        RamScanCapturer(LevelConditionsTransformer* l) : context(l) {}

        bool getModified() const {
            return modified;
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            if (auto* scan = dynamic_cast<RamScan*>(node.get())) {
                RamFilterCapturer filterUpdate(context, scan->getIdentifier());
                node->apply(filterUpdate);

                // If a condition applies to this scan level, filter the scan based on the condition
                if (std::unique_ptr<RamCondition> condition = filterUpdate.getCondition()) {
                    RamFilterInsert filterInsert(std::move(condition));
                    node->apply(filterInsert);
                    modified = true;
                }
            }

            node->apply(*this);
            return node;
        }
    };

    // Node-mapper that searches for and updates RAM inserts
    class RamQueryCapturer : public RamNodeMapper {
        mutable bool modified = false;
        LevelConditionsTransformer* context;

    public:
        RamQueryCapturer(LevelConditionsTransformer* l) : context(l) {}

        bool getModified() const {
            return modified;
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            // get all RAM inserts
            if (auto* insert = dynamic_cast<RamQuery*>(node.get())) {
                RamScanCapturer scanUpdate(context);
                insert->apply(scanUpdate);

                if (scanUpdate.getModified()) {
                    modified = true;
                }
            } else {
                // no need to search for nested RAM inserts
                node->apply(*this);
            }

            return node;
        }
    };

    // level all RAM inserts
    RamQueryCapturer insertUpdate(this);
    program.getMain()->apply(insertUpdate);

    return insertUpdate.getModified();
}

/** Get indexable element */
std::unique_ptr<RamExpression> CreateIndicesTransformer::getIndexElement(
        RamCondition* c, size_t& element, size_t identifier) {
    if (auto* binRelOp = dynamic_cast<RamConstraint*>(c)) {
        if (binRelOp->getOperator() == BinaryConstraintOp::EQ) {
            if (auto* lhs = dynamic_cast<RamElementAccess*>(binRelOp->getLHS())) {
                RamExpression* rhs = binRelOp->getRHS();
                if (rvla->getLevel(lhs) == identifier &&
                        (rcva->isConstant(rhs) || rvla->getLevel(rhs) < identifier)) {
                    element = lhs->getElement();
                    return std::unique_ptr<RamExpression>(rhs->clone());
                }
            }
            if (auto* rhs = dynamic_cast<RamElementAccess*>(binRelOp->getRHS())) {
                RamExpression* lhs = binRelOp->getLHS();
                if (rvla->getLevel(rhs) == identifier &&
                        (rcva->isConstant(lhs) || rvla->getLevel(lhs) < identifier)) {
                    element = rhs->getElement();
                    return std::unique_ptr<RamExpression>(lhs->clone());
                }
            }
        }
    }
    return nullptr;
}

std::unique_ptr<RamOperation> CreateIndicesTransformer::rewriteScan(const RamScan* scan) {
    if (const auto* filter = dynamic_cast<const RamFilter*>(&scan->getOperation())) {
        const RamRelation& rel = scan->getRelation();
        const size_t identifier = scan->getIdentifier();

        // Values of index per column of table (if indexable)
        std::vector<std::unique_ptr<RamExpression>> queryPattern(rel.getArity());

        // Remaining conditions which weren't handled by an index
        std::unique_ptr<RamCondition> condition;

        auto addCondition = [&](std::unique_ptr<RamCondition> c) {
            if (condition != nullptr) {
                condition = std::make_unique<RamConjunction>(std::move(condition), std::move(c));
            } else {
                condition = std::move(c);
            }
        };

        bool indexable = false;

        for (auto& cond : getConditions(&filter->getCondition())) {
            size_t element = 0;
            if (std::unique_ptr<RamExpression> value = getIndexElement(cond.get(), element, identifier)) {
                indexable = true;
                if (queryPattern[element] == nullptr) {
                    queryPattern[element] = std::move(value);
                } else {
                    addCondition(std::move(cond));
                }
            } else {
                addCondition(std::move(cond));
            }
        }

        if (indexable) {
            // replace scan by index scan
            return std::make_unique<RamIndexScan>(std::make_unique<RamRelationReference>(&rel), identifier,
                    std::move(queryPattern),
                    condition == nullptr
                            ? std::unique_ptr<RamOperation>(filter->getOperation().clone())
                            : std::make_unique<RamFilter>(std::move(condition),
                                      std::unique_ptr<RamOperation>(filter->getOperation().clone())),
                    scan->getProfileText());
        }
    }

    return nullptr;
}

bool CreateIndicesTransformer::createIndices(RamProgram& program) {
    // TODO: Change these to LambdaRamNodeMapper lambdas
    // Node-mapper that searches for and updates RAM scans nested in RAM inserts
    class RamScanCapturer : public RamNodeMapper {
        mutable bool modified = false;
        CreateIndicesTransformer* context;

    public:
        RamScanCapturer(CreateIndicesTransformer* c) : context(c) {}

        bool getModified() const {
            return modified;
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            if (auto* scan = dynamic_cast<RamScan*>(node.get())) {
                if (std::unique_ptr<RamOperation> op = context->rewriteScan(scan)) {
                    modified = true;
                    node = std::move(op);
                }
            }
            node->apply(*this);
            return node;
        }
    };

    // Node-mapper that searches for and updates RAM inserts
    class RamQueryCapturer : public RamNodeMapper {
        mutable bool modified;
        CreateIndicesTransformer* context;

    public:
        RamQueryCapturer(CreateIndicesTransformer* c) : modified(false), context(c) {}

        bool getModified() const {
            return modified;
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            // get all RAM inserts
            if (auto* insert = dynamic_cast<RamQuery*>(node.get())) {
                RamScanCapturer scanUpdate(context);
                insert->apply(scanUpdate);
                if (!modified && scanUpdate.getModified()) {
                    modified = true;
                }
            } else {
                // no need to search for nested RAM inserts
                node->apply(*this);
            }
            return node;
        }
    };

    // level all RAM inserts
    RamQueryCapturer insertUpdate(this);
    program.getMain()->apply(insertUpdate);

    return insertUpdate.getModified();
}

bool ConvertExistenceChecksTransformer::convertExistenceChecks(RamProgram& program) {
    // TODO: Change these to LambdaRamNodeMapper lambdas
    // Node-mapper that searches for and updates RAM scans nested in RAM inserts

    class RamScanCapturer : public RamNodeMapper {
        mutable bool modified = false;
        ConvertExistenceChecksTransformer* context;

    public:
        RamScanCapturer(ConvertExistenceChecksTransformer* c) : context(c) {}

        bool getModified() const {
            return modified;
        }

		// similar to first getDepthFirst
        bool dependsOn(const RamExpression* value, const size_t identifier) const {
            std::vector<const RamExpression*> queue = {value};
            while (!queue.empty()) {
                const RamExpression* val = queue.back();
                queue.pop_back();
                if (const auto* elemAccess = dynamic_cast<const RamElementAccess*>(val)) {
                    if (context->rvla->getLevel(elemAccess) == identifier) {
                        return true;
                    }
                } else if (const auto* intrinsicOp = dynamic_cast<const RamIntrinsicOperator*>(val)) {
                    for (const RamExpression* arg : intrinsicOp->getArguments()) {
                        queue.push_back(arg);
                    }
                } else if (const auto* userDefinedOp = dynamic_cast<const RamUserDefinedOperator*>(val)) {
                    for (const RamExpression* arg : userDefinedOp->getArguments()) {
                        queue.push_back(arg);
                    }
                }
            }
            return false;
        }

        bool dependsOn(const RamCondition* condition, const size_t identifier) const {
            if (const auto* binRel = dynamic_cast<const RamConstraint*>(condition)) {
                return dependsOn(binRel->getLHS(), identifier) || dependsOn(binRel->getRHS(), identifier);
            }
            return false;
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            if (auto* scan = dynamic_cast<RamRelationSearch*>(node.get())) {
                const size_t identifier = scan->getIdentifier();
                bool isExistCheck = true;
                visitDepthFirst(scan->getOperation(), [&](const RamProject& project) { // look for all the things of this type (lambda)
                    if (isExistCheck) {
                        std::vector<const RamExpression*> values;
                        // TODO: function to extend vectors
                        const std::vector<RamExpression*> initialVals = project.getValues(); // e.g. t0.a
                        values.insert(values.end(), initialVals.begin(), initialVals.end());

						// element in the projection can be a record and so need to recurse
                        while (!values.empty()) {
                            const RamExpression* value = values.back();
                            values.pop_back();

                            if (const auto* pack = dynamic_cast<const RamPackRecord*>(value)) {
                                const std::vector<RamExpression*> args = pack->getArguments();
                                values.insert(values.end(), args.begin(), args.end());
                            } else if (const auto* intrinsicOp =
                                               dynamic_cast<const RamIntrinsicOperator*>(value)) {
                                for (auto* arg : intrinsicOp->getArguments()) {
                                    values.push_back(arg);
                                }
                            } else if (value != nullptr && !context->rcva->isConstant(value) &&
                                       context->rvla->getLevel(value) == identifier) {
                                isExistCheck = false;
                                break;
                            }
                        }
                    }
                });
                if (isExistCheck) {
                    visitDepthFirst(scan->getOperation(), [&](const RamUnpackRecord& lookup) {
                        if (isExistCheck) {
                            if (lookup.getReferenceLevel() == identifier) {
                                isExistCheck = false;
                            }
                        }
                    });
                }
                if (isExistCheck) {
                    visitDepthFirst(scan->getOperation(), [&](const RamExpression& expression) {
                        if (isExistCheck) {
                            if (dependsOn(&expression, identifier)) {
                                isExistCheck = false;
                            }
                        }
                    });
                }
                if (isExistCheck) {
                    // create constraint
                    std::unique_ptr<RamCondition> constraint;

                    if (nullptr != dynamic_cast<RamScan*>(scan)) {
                        constraint = std::make_unique<RamNegation>(std::make_unique<RamEmptinessCheck>(
                                std::make_unique<RamRelationReference>(&scan->getRelation())));
                    } else if (auto* indexScan = dynamic_cast<RamIndexScan*>(scan)) {
                        std::vector<std::unique_ptr<RamExpression>> values;
                        for (RamExpression* value : indexScan->getRangePattern()) {
                            if (value != nullptr) {
                                values.emplace_back(value->clone());
                            } else {
                                values.push_back(nullptr);
                            }
                        }
                        constraint = std::make_unique<RamExistenceCheck>(
                                std::make_unique<RamRelationReference>(&scan->getRelation()),
                                std::move(values));
                    }

                    node = std::make_unique<RamFilter>(std::move(constraint),
                            std::unique_ptr<RamOperation>(scan->getOperation().clone()),
                            scan->getProfileText());
                    modified = true;
                }
            }
            node->apply(*this);
            return node;
        }
    };

    // Node-mapper that searches for and updates RAM inserts
    class RamQueryCapturer : public RamNodeMapper {
        mutable bool modified;
        ConvertExistenceChecksTransformerNestedSearchIf* context;

    public:
        RamQueryCapturer(ConvertExistenceChecksTransformer* c) : modified(false), context(c) {}

        bool getModified() const {
            return modified;
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            // get all RAM inserts (RamQuery = RamInsert)
            if (auto* insert = dynamic_cast<RamQuery*>(node.get())) {
                RamScanCapturer scanUpdate(context);
                insert->apply(scanUpdate);

                if (scanUpdate.getModified()) {
                    modified = true;
                }
            } else {
                // no need to search for nested RAM inserts
                node->apply(*this);
            }
            return node;
        }
    };

    // level all RAM inserts
    RamQueryCapturer insertUpdate(this);
    program.getMain()->apply(insertUpdate);

    return insertUpdate.getModified();
}

bool SearchesToChoicesTransformer::searchesToChoices(RamProgram& program) {
	// Node mapper
//    class RamSearchIfCapturer : public RamNodeMapper {
//        mutable bool modified;
//        ConvertExistenceChecksTransformer* context;

	// 1) Find all such statements "SEARCH X ..." and subsequent if statement
	// 2) check if not referred to
	// 3) replacement with CHOICE
	// RamElementAccess = t.x
	// Expression = whole thing
	// DFS from projection - store list of elements from tuples seen thus far, store element in previous level, then when reach SEARCH check if anywhere but previous level

	class RamScanCapturer : public RamNodeMapper {
        mutable bool modified = false;
        SearchesToChoicesTransformer* context;

    public:
        RamScanCapturer(SearchesToChoicesTransformer* c) : modified(false), context(c) {}

        bool getModified() const {
            return modified;
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            if (auto* scan = dynamic_cast<RamRelationSearch*>(node.get())) { // if Scan/ScanIndex etc.

			}
            node->apply(*this);
            return node;
		}
	};

	// Node mapper
    class RamQueryCapturer : public RamNodeMapper {
        mutable bool modified = false;
        SearchesToChoicesTransformer* context;

    public:
        RamQueryCapturer(SearchesToChoicesTransformer* c) : modified(false), context(c) {}

        bool getModified() const {
            return modified;
        }

        std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
            // get all RAM inserts
            if (auto* insert = dynamic_cast<RamQuery*>(node.get())) {
                RamScanCapturer scanUpdate(context);
                insert->apply(scanUpdate);

                if (scanUpdate.getModified()) {
                    modified = true;
                }
            } else {
                // no need to search for nested RAM inserts
                node->apply(*this);
            }
            return node;
        }
    };

    // level all RAM inserts
    RamQueryCapturer insertUpdate(this);
    program.getMain()->apply(insertUpdate);

    return insertUpdate.getModified();

}

}  // end of namespace souffle
