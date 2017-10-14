/*
Copyright 2013-present Barefoot Networks, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "analyzer.h"

#include "ir/ir.h"
#include "frontends/p4/fromv1.0/v1model.h"
#include "frontends/p4/typeMap.h"
#include "frontends/common/resolveReferences/referenceMap.h"
#include "frontends/p4/methodInstance.h"
#include "frontends/p4/tableApply.h"

#include "lib/ordered_map.h"
#include "lib/ordered_set.h"

namespace BMV2 {

unsigned CFG::Node::crtId = 0;

void CFG::EdgeSet::dbprint(std::ostream& out) const {
    for (auto s : edges)
        out << " " << s;
}

void CFG::Edge::dbprint(std::ostream& out) const {
    out << endpoint->name;
    switch (type) {
        case EdgeType::True:
            out << "(true)";
            break;
        case EdgeType::False:
            out << "(false)";
            break;
        case EdgeType::Label:
            out << "(" << label << ")";
            break;
        default:
            break;
    }
}

void CFG::Node::dbprint(std::ostream& out) const
{ out << name << " =>" << successors; }

void CFG::dbprint(std::ostream& out, CFG::Node* node, std::set<CFG::Node*> &done) const {
    if (done.find(node) != done.end())
        return;
    for (auto p : node->predecessors.edges)
        dbprint(out, p->endpoint, done);
    out << std::endl << node;
    done.emplace(node);
}

void CFG::dbprint(std::ostream& out) const {
    out << "CFG for " << container;
    std::set<CFG::Node*> done;
    for (auto n : allNodes)
        dbprint(out, n, done);
}

void CFG::Node::computeSuccessors() {
    for (auto e : predecessors.edges)
        e->getNode()->successors.emplace(e->clone(this));
}

bool CFG::dfs(Node* node, std::set<Node*> &visited,
              std::set<const IR::P4Table*> &stack) const {
    const IR::P4Table* table = nullptr;
    if (node->is<TableNode>()) {
        table = node->to<TableNode>()->table;
        if (stack.find(table) != stack.end()) {
            ::error("Program cannot be implemented since it requires a cycle containing %1%",
                    table);
            return false;
        }
    }
    if (visited.find(node) != visited.end())
        return true;
    if (table != nullptr)
        stack.emplace(table);
    for (auto e : node->successors.edges) {
        bool success = dfs(e->endpoint, visited, stack);
        if (!success) return false;
    }
    if (table != nullptr)
        stack.erase(table);
    visited.emplace(node);
    return true;
}

bool CFG::checkForCycles() const {
    std::set<Node*> visited;
    std::set<const IR::P4Table*> stack;
    for (auto n : allNodes) {
        bool success = dfs(n, visited, stack);
        if (!success) return false;
    }
    return true;
}

namespace {
// This visitor "converts" IR::Node* into EdgeSets
// Since we cannot return EdgeSets, we place them in the 'after' map.
class CFGBuilder final : public FastInspector<CFGBuilder> {
    CFG*                    cfg;
    const CFG::EdgeSet*     current;  // predecessors of current node
    std::map<const IR::Statement*, const CFG::EdgeSet*> after;  // successors of each statement
    P4::ReferenceMap*       refMap;
    P4::TypeMap*            typeMap;

    void setAfter(const IR::Statement* statement, const CFG::EdgeSet* value) {
        LOG1("After " << statement << " " << value);
        CHECK_NULL(statement);
        if (value == nullptr)
            // This can happen when an error is signaled
            return;
        after.emplace(statement, value);
        current = value;
    }

    const CFG::EdgeSet* get(const IR::Statement* statement)
    { return ::get(after, statement); }

 public:
    bool preorder(const IR::Statement* statement) override {
        ::error("%1%: not supported in control block on this architecture", statement);
        return false;
    }
    bool preorder(const IR::ReturnStatement* statement) override {
        cfg->exitPoint->addPredecessors(current);
        setAfter(statement, new CFG::EdgeSet());  // empty successor set
        return false;
    }
    bool preorder(const IR::ExitStatement* statement) override {
        cfg->exitPoint->addPredecessors(current);
        setAfter(statement, new CFG::EdgeSet());  // empty successor set
        return false;
    }
    bool preorder(const IR::EmptyStatement* statement) override {
        // unchanged 'current'
        setAfter(statement, current);
        return false;
    }
    bool preorder(const IR::MethodCallStatement* statement) override {
        auto instance = P4::MethodInstance::resolve(statement->methodCall, refMap, typeMap);
        if (!instance->is<P4::ApplyMethod>())
            return false;
        auto am = instance->to<P4::ApplyMethod>();
        if (!am->object->is<IR::P4Table>()) {
            ::error("%1%: apply method must be on a table", statement);
            return false;
        }
        auto tc = am->object->to<IR::P4Table>();
        auto node = cfg->makeNode(tc, statement->methodCall);
        node->addPredecessors(current);
        setAfter(statement, new CFG::EdgeSet(new CFG::Edge(node)));
        return false;
    }
    bool preorder(const IR::IfStatement* statement) override {
        // We only allow expressions of the form t.apply().hit currently.
        // If the expression is more complex it should have been
        // simplified by prior passes.
        auto tc = P4::TableApplySolver::isHit(statement->condition, refMap, typeMap);
        CFG::Node* node;
        if (tc != nullptr) {
            // hit-miss case.
            node = cfg->makeNode(tc, statement->condition);
        } else {
            node = cfg->makeNode(statement);
        }

        node->addPredecessors(current);
        // If branch
        current = new CFG::EdgeSet(new CFG::Edge(node, true));
        visit(statement->ifTrue);
        auto ifTrue = get(statement->ifTrue);
        if (ifTrue == nullptr)
            return false;
        auto result = new CFG::EdgeSet(ifTrue);
        // Else branch
        if (statement->ifFalse != nullptr) {
            current = new CFG::EdgeSet(new CFG::Edge(node, false));
            visit(statement->ifFalse);
            auto ifFalse = get(statement->ifFalse);
            result->mergeWith(ifFalse);
        } else {
            // no else branch
            result->mergeWith(new CFG::EdgeSet(new CFG::Edge(node, false)));
        }
        setAfter(statement, result);
        return false;
    }
    bool preorder(const IR::BlockStatement* statement) override {
        for (auto s : statement->components) {
            auto stat = s->to<IR::Statement>();
            if (stat == nullptr) continue;
            visit(stat);
            current = get(stat);
        }
        setAfter(statement, current);
        return false;
    }
    bool preorder(const IR::SwitchStatement* statement) override {
        auto tc = P4::TableApplySolver::isActionRun(statement->expression, refMap, typeMap);
        BUG_CHECK(tc != nullptr, "%1%: unexpected switch statement expression",
                  statement->expression);
        auto node = cfg->makeNode(tc, statement->expression);
        node->addPredecessors(current);
        auto result = new CFG::EdgeSet(new CFG::Edge(node));  // In case no label matches
        auto labels = new CFG::EdgeSet();
        for (auto sw : statement->cases) {
            cstring label;
            if (sw->label->is<IR::DefaultExpression>()) {
                label = "default";
            } else {
                auto pe = sw->label->to<IR::PathExpression>();
                CHECK_NULL(pe);
                label = pe->path->name.name;
            }
            labels->mergeWith(new CFG::EdgeSet(new CFG::Edge(node, label)));
            if (sw->statement != nullptr) {
                current = labels;
                visit(sw->statement);
                labels = new CFG::EdgeSet();
            }  // else we accumulate edges
            result->mergeWith(current);
        }
        setAfter(statement, result);
        return false;
    }

    CFGBuilder(CFG* cfg, P4::ReferenceMap* refMap, P4::TypeMap* typeMap) :
            cfg(cfg), current(nullptr), refMap(refMap), typeMap(typeMap) {}

    const CFG::EdgeSet* run(const IR::Statement* startNode, const CFG::EdgeSet* predecessors) {
        CHECK_NULL(startNode); CHECK_NULL(predecessors);
        current = predecessors;
        startNode->apply(*this);
        return current;
    }
};
}  // end anonymous namespace

void CFG::build(const IR::P4Control* cc,
                P4::ReferenceMap* refMap, P4::TypeMap* typeMap) {
    container = cc;
    entryPoint = makeNode(cc->name + ".entry");
    exitPoint = makeNode("");  // the only node with an empty name

    CFGBuilder builder(this, refMap, typeMap);
    auto startValue = new CFG::EdgeSet(new CFG::Edge(entryPoint));
    auto last = builder.run(cc->body, startValue);
    LOG1("Before exit " << last);
    if (last != nullptr) {
        // nullptr can happen when there is an error
        exitPoint->addPredecessors(last);
        computeSuccessors();
    }
}

void DiscoverStructure::postorder(const IR::ParameterList* paramList) {
    bool inAction = findContext<IR::P4Action>() != nullptr;
    unsigned index = 0;
    for (auto p : *paramList->getEnumerator()) {
        structure->index.emplace(p, index);
        if (!inAction)
            structure->nonActionParameters.emplace(p);
        index++;
    }
}

void DiscoverStructure::postorder(const IR::P4Action* action) {
    LOG1("discovery action " << action);
    auto control = findContext<IR::P4Control>();
    structure->actions.emplace(action, control);
}

void DiscoverStructure::postorder(const IR::Declaration_Variable* decl) {
    structure->variables.push_back(decl);
}

}  // namespace BMV2
