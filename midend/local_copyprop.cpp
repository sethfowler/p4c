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

#include "local_copyprop.h"
#include "has_side_effects.h"
#include "expr_uses.h"

class P4::DoLocalCopyPropagation::ElimDead : public Transform {
    DoLocalCopyPropagation &self;
    const IR::Node *preorder(IR::Declaration_Variable *var) override {
        if (auto local = ::getref(self.available, var->name)) {
            BUG_CHECK(local->local, "Non-local local var?");
            if (!local->live) {
                LOG3("  removing dead local " << var->name);
                return nullptr; }
        } else {
            BUG("local %s isn't in available table", var->name); }
        return var; }
    IR::AssignmentStatement *postorder(IR::AssignmentStatement *as) override {
        if (auto dest = as->left->to<IR::PathExpression>()) {
            if (auto var = ::getref(self.available, dest->path->name)) {
                if (var->local && !var->live) {
                    LOG3("  removing dead assignment to " << dest->path->name);
                    return nullptr;
                } else if (var->local) {
                    LOG6("  not removing live assignment to " << dest->path->name);
                } else {
                    LOG6("  not removing assignment to non-local " << dest->path->name); } } }
        return as; }
    IR::IfStatement *postorder(IR::IfStatement *s) override {
        if (s->ifTrue == nullptr) {
            /* can't leave ifTrue == nullptr, as that will fail validation -- fold away
             * the if statement as needed */
            if (s->ifFalse == nullptr)
                return nullptr;
            s->ifTrue = s->ifFalse;
            s->ifFalse = nullptr;
            s->condition = new IR::LNot(s->condition); }
        return s; }

 public:
    explicit ElimDead(DoLocalCopyPropagation &self) : self(self) {}
};

void P4::DoLocalCopyPropagation::flow_merge(Visitor &a_) {
    auto &a = dynamic_cast<DoLocalCopyPropagation &>(a_);
    BUG_CHECK(working == a.working, "inconsitent DoLocalCopyPropagation state on merge");
    for (auto &var : available) {
        if (auto merge = ::getref(a.available, var.first)) {
            if (merge->val != var.second.val)
                var.second.val = nullptr;
            if (merge->live)
                var.second.live = true;
        } else {
            var.second.val = nullptr; } }
}

void P4::DoLocalCopyPropagation::dropValuesUsing(cstring name) {
    LOG6("dropValuesUsing(" << name << ")");
    for (auto &var : available) {
        LOG7("  checking " << var.first << " = " << var.second.val);
        if (var.first == name) {
            LOG4("   dropping " << name << " as it is being assigned to");
            var.second.val = nullptr;
        } else if (var.second.val && exprUses(var.second.val, name)) {
            LOG4("   dropping " << var.first << " as it uses " << name);
            var.second.val = nullptr; } }
}

void P4::DoLocalCopyPropagation::visit_local_decl(const IR::Declaration_Variable *var) {
    LOG4("Visiting " << var);
    if (available.count(var->name))
        BUG("duplicate var declaration for %s", var->name);
    auto &local = available[var->name];
    local.local = true;
    if (var->initializer) {
        if (!hasSideEffects(var->initializer)) {
            LOG3("  saving init value for " << var->name << ": " << var->initializer);
            local.val = var->initializer;
        } else {
            local.live = true; } }
}

const IR::Node *P4::DoLocalCopyPropagation::postorder(IR::Declaration_Variable *var) {
    if (!working) return var;
    visit_local_decl(var);
    return var;
}

const IR::Expression *P4::DoLocalCopyPropagation::postorder(IR::PathExpression *path) {
    if (!working) return path;
    if (isWrite()) {
        dropValuesUsing(path->path->name);
        return path; }
    if (auto var = ::getref(available, path->path->name)) {
        if (var->val) {
            LOG3("  propagating value for " << path->path->name << ": " << var->val);
            return var->val;
        } else {
            LOG4("  using " << path->path->name << " with no propagated value");
            var->live = true; } }
    return path;
}

IR::AssignmentStatement *P4::DoLocalCopyPropagation::preorder(IR::AssignmentStatement *as) {
    if (!working) return as;
    // visit the source subtree first, before the destination subtree
    // make sure child indexes are set properly so we can detect writes -- these are the
    // extra arguments to 'visit' in order to make introspection vis the Visitor::Context
    // work.  Normally this is all managed by the auto-generated visit_children methods,
    // but since we're overriding that here AND P4WriteContext cares about it (that's how
    // it determines whether something is a write or a read), it needs to be correct
    // This is error-prone and fragile
    visit(as->right, "right", 1);
    visit(as->left, "left", 0);
    prune();
    return postorder(as);
}

IR::AssignmentStatement *P4::DoLocalCopyPropagation::postorder(IR::AssignmentStatement *as) {
    if (as->left == as->right) {   // FIXME -- need deep equals here?
        LOG3("  removing noop assignment " << *as);
        return nullptr; }
    if (!working) return as;
    if (auto dest = as->left->to<IR::PathExpression>()) {
        if (!hasSideEffects(as->right)) {
            if (as->right->is<IR::ListExpression>()) {
                /* FIXME -- List Expressions need to be turned into constructor calls before
                 * we can copyprop them */
                return as; }
            if (exprUses(as->right, dest->path->name)) {
                /* can't propagate the value as it is defined in terms of itself.
                 * FIXME -- we could propagate if we introduced a new temp, but that
                 * may make things worse rather than better */
                return as; }
            LOG3("  saving value for " << dest->path->name << ": " << as->right);
            available[dest->path->name].val = as->right; }
    } else {
        LOG3("dest of assignment is " << as->left << " so skipping");
    }
    return as;
}

IR::MethodCallExpression *P4::DoLocalCopyPropagation::postorder(IR::MethodCallExpression *mc) {
    if (!working) return mc;
    LOG3("method call " << mc->method << "clears all saved values");
    /* FIXME -- should look at what the called method actually reads and modifies, rather
     * than assuming everything */
    for (auto &var : Values(available)) {
        var.val = nullptr;
        var.live = true; }
    return mc;
}

IR::P4Action *P4::DoLocalCopyPropagation::preorder(IR::P4Action *act) {
    if (working || !available.empty()) BUG("corrupt internal data struct");
    working = true;
    LOG2("DoLocalCopyPropagation working on action " << act->name);
    LOG4(act);
    return act;
}

IR::P4Action *P4::DoLocalCopyPropagation::postorder(IR::P4Action *act) {
    LOG5("DoLocalCopyPropagation before ElimDead " << act->name);
    LOG5(act);
    act->body = act->body->apply(ElimDead(*this))->to<IR::BlockStatement>();
    working = false;
    available.clear();
    LOG3("DoLocalCopyPropagation finished action " << act->name);
    LOG4(act);
    return act;
}

IR::P4Control *P4::DoLocalCopyPropagation::preorder(IR::P4Control *ctrl) {
    if (working || !available.empty()) BUG("corrupt internal data struct");
    visit(ctrl->type, "type");
    visit(ctrl->constructorParams, "constructorParams");
    visit(ctrl->controlLocals, "controlLocals");
    if (working || !available.empty()) BUG("corrupt internal data struct");
    working = true;
    LOG2("DoLocalCopyPropagation working on control " << ctrl->name);
    LOG4(ctrl);
    for (auto local : *ctrl->controlLocals)
        if (auto var = local->to<IR::Declaration_Variable>())
            visit_local_decl(var);
    visit(ctrl->body, "body");
    LOG5("DoLocalCopyPropagation before ElimDead " << ctrl->name);
    LOG5(ctrl);
    ctrl->body = ctrl->body->apply(ElimDead(*this))->to<IR::BlockStatement>();
    working = false;
    available.clear();
    LOG3("DoLocalCopyPropagation finished control " << ctrl->name);
    LOG4(ctrl);
    prune();
    return ctrl;
}
