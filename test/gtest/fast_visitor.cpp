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

#include <boost/optional.hpp>
#include "gtest/gtest.h"
#include "helpers.h"
#include "ir/ir.h"
#include "ir/visitor.h"

namespace Test {

/*
template <typename Visitor>
boost::optional<uint64_t> getNodeClassIdIfPresent(...) { return boost::none; }

template <typename Visitor, typename T>
auto getNodeClassIdIfPresent(const T*)
  -> decltype(((void)T::nodeClassId), boost::optional<uint64_t>())
{ return T::nodeClassId; }

// We'll pretend as if `IR::If` doesn't have a class ID, because it's an awkward
// case for the tests below; it's the only case of a class that can be
// instantiated that has a subclass. That makes it the only leaf class that has
// a non-singleton match ID.
template <typename Visitor>
boost::optional<uint64_t> getNodeClassIdIfPresent(const IR::If*)
{ return boost::none; }

TEST(FastInspector, MatchIds) {
#define CHECK_FASTINSPECTOR_MATCH_IDS(CLASS, _)                                     \
      struct FastInspectorTest_##CLASS : public FastInspector<FastInspectorTest_##CLASS> { \
          bool preorder(const IR::CLASS*) override { return true; } \
      }; \
 \
      { \
          using FastInspectorType = FastInspectorTest_##CLASS; \
          FastInspectorType instance; \
          EXPECT_EQ(IR::CLASS::getNodeMatchedIds(), instance.visitedNodeClasses); \
          std::cerr << "Checked match ids for " << IR::CLASS::static_type_name() << std::endl; \
          auto nodeClassId = \
            getNodeClassIdIfPresent<FastInspectorType>(static_cast<IR::CLASS*>(nullptr)); \
          if (nodeClassId) { \
              ReachableNodeSet justThisClass; \
              justThisClass.set(*nodeClassId); \
              EXPECT_EQ(justThisClass, instance.visitedNodeClasses); \
              std::cerr << "Checked match ids for concrete class " << IR::CLASS::static_type_name() << std::endl; \
          } \
      } \

IRNODE_ALL_NON_TEMPLATE_SUBCLASSES(CHECK_FASTINSPECTOR_MATCH_IDS)
#undef CHECK_FASTINSPECTOR_MATCH_IDS
}
*/

static boost::optional<FrontendTestCase> getFastInspectorTestCase() {
    return FrontendTestCase::create(P4_SOURCE(P4Headers::V1MODEL, R"(
        struct Headers { }
        struct Metadata { }
        parser parse(packet_in p, out Headers h, inout Metadata m,
                     inout standard_metadata_t sm) {
            state start { transition accept; } }
        control verifyChecksum(inout Headers h, inout Metadata m) { apply { } }
        control egress(inout Headers h, inout Metadata m,
                        inout standard_metadata_t sm) { apply { } }
        control computeChecksum(inout Headers h, inout Metadata m) { apply { } }
        control deparse(packet_out p, in Headers h) { apply { } }

        control ingress(inout Headers h, inout Metadata m,
                        inout standard_metadata_t sm) {
            action noop() { }

            table igTable {
                actions = { noop; }
                default_action = noop;
            }

            @name("igTableWithName")
            table igTableWithoutName {
                actions = { noop; }
                default_action = noop;
            }

            @id(1234)
            table igTableWithId {
                actions = { noop; }
                default_action = noop;
            }

            @id(5678)
            @name("igTableWithNameAndId")
            table igTableWithoutNameAndId {
                actions = { noop; }
                default_action = noop;
            }

            @id(4321)
            table conflictingTableA {
                actions = { noop; }
                default_action = noop;
            }

            @id(4321)
            table conflictingTableB {
                actions = { noop; }
                default_action = noop;
            }

            apply {
                igTable.apply();
                igTableWithoutName.apply();
                igTableWithId.apply();
                igTableWithoutNameAndId.apply();
                conflictingTableA.apply();
                conflictingTableB.apply();
            }
        }

        V1Switch(parse(), verifyChecksum(), ingress(), egress(),
                 computeChecksum(), deparse()) main;
    )"));
}

TEST(FastInspector, VisitedNodes) {
    auto test = getFastInspectorTestCase();
    ASSERT_TRUE(test);

    struct ClearReachability : public Inspector {
        ClearReachability() { markReachableDirty = true; }
    };
    ClearReachability clearReachability;
    test->program->apply(clearReachability);

    struct CountPaths : public Inspector {
        unsigned preorderCount = 0;
        unsigned postorderCount = 0;
        unsigned otherPreorderCount = 0;
        unsigned otherPostorderCount = 0;
        bool preorder(const IR::Path*) override {
            preorderCount++;
            return true;
        }
        void postorder(const IR::Path*) override { postorderCount++; }
        bool preorder(const IR::Node*) override {
            otherPreorderCount++;
            return true;
        }
        void postorder(const IR::Node*) override { otherPostorderCount++; }
    };
    CountPaths inspector;
    test->program->apply(inspector);
    EXPECT_GT(inspector.preorderCount, 0u);
    EXPECT_GT(inspector.postorderCount, 0u);
    EXPECT_GT(inspector.otherPreorderCount, 0u);
    EXPECT_GT(inspector.otherPostorderCount, 0u);

    struct FastCountPaths : public FastInspector<FastCountPaths> {
        unsigned preorderCount = 0;
        unsigned postorderCount = 0;
        bool preorder(const IR::Path*) override {
            preorderCount++;
            return true;
        }
        void postorder(const IR::Path*) override { postorderCount++; }
    };
    FastCountPaths fastInspector;
    test->program->apply(fastInspector);
    EXPECT_GT(fastInspector.preorderCount, 0u);
    EXPECT_GT(fastInspector.postorderCount, 0u);

    EXPECT_EQ(inspector.preorderCount, fastInspector.preorderCount);
    EXPECT_EQ(inspector.postorderCount, fastInspector.postorderCount);

    CountPaths suspiciousInspector;
    suspiciousInspector.visitedNodeClasses = fastInspector.visitedNodeClasses;
    test->program->apply(suspiciousInspector);
    EXPECT_EQ(inspector.preorderCount, suspiciousInspector.preorderCount);
    EXPECT_EQ(inspector.postorderCount, suspiciousInspector.postorderCount);
    EXPECT_LT(suspiciousInspector.otherPreorderCount, inspector.otherPreorderCount);
    EXPECT_LT(suspiciousInspector.otherPostorderCount, inspector.otherPostorderCount);

    std::cerr << "** Normal preorder count: " << inspector.preorderCount << std::endl;
    std::cerr << "** Normal postorder count: " << inspector.postorderCount << std::endl;
    std::cerr << "** Normal preorder (other) count: " << inspector.otherPreorderCount << std::endl;
    std::cerr << "** Normal postorder (other) count: " << inspector.otherPostorderCount << std::endl;
    std::cerr << "** Suspicious preorder count: " << suspiciousInspector.preorderCount << std::endl;
    std::cerr << "** Suspicious postorder count: " << suspiciousInspector.postorderCount << std::endl;
    std::cerr << "** Suspicious preorder (other) count: " << suspiciousInspector.otherPreorderCount << std::endl;
    std::cerr << "** Suspicious postorder (other) count: " << suspiciousInspector.otherPostorderCount << std::endl;

    auto secondTest = getFastInspectorTestCase();
    ASSERT_TRUE(secondTest);
    secondTest->program->apply(clearReachability);

    struct BadCountPaths : public Inspector {
        unsigned preorderCount = 0;
        unsigned postorderCount = 0;
        unsigned otherPreorderCount = 0;
        unsigned otherPostorderCount = 0;
        bool preorder(const IR::Path*) override {
            preorderCount++;
            return true;
        }
        void postorder(const IR::Path*) override { postorderCount++; }
        bool preorder(const IR::Node*) override {
            otherPreorderCount++;
            return true;
        }
        void postorder(const IR::Node*) override { otherPostorderCount++; }
        bool preorder(const IR::P4Table*) override { return false; }
        bool preorder(const IR::P4Action*) override { return false; }
        bool preorder(const IR::P4Control*) override { return false; }
        bool preorder(const IR::P4Parser*) override { return false; }
        bool preorder(const IR::Type_StructLike*) override { return false; }
    };
    BadCountPaths badInspector;
    secondTest->program->apply(badInspector);
    EXPECT_GT(badInspector.preorderCount, 0u);
    EXPECT_GT(badInspector.postorderCount, 0u);
    EXPECT_GT(badInspector.otherPreorderCount, 0u);
    EXPECT_GT(badInspector.otherPostorderCount, 0u);
    EXPECT_TRUE(secondTest->program->reachableNodeClassesIsDirty);

    std::cerr << "** Bad preorder count: " << badInspector.preorderCount << std::endl;
    std::cerr << "** Bad postorder count: " << badInspector.postorderCount << std::endl;
    std::cerr << "** Bad preorder (other) count: " << badInspector.otherPreorderCount << std::endl;
    std::cerr << "** Bad postorder (other) count: " << badInspector.otherPostorderCount << std::endl;

    CountPaths secondFastInspector;
    secondFastInspector.visitedNodeClasses = fastInspector.visitedNodeClasses;
    secondTest->program->apply(secondFastInspector);
    EXPECT_GT(secondFastInspector.preorderCount, 0u);
    EXPECT_GT(secondFastInspector.postorderCount, 0u);

    EXPECT_EQ(inspector.preorderCount, secondFastInspector.preorderCount);
    EXPECT_EQ(inspector.postorderCount, secondFastInspector.postorderCount);

    std::cerr << "** Second suspicious preorder count: " << secondFastInspector.preorderCount << std::endl;
    std::cerr << "** Second suspicious postorder count: " << secondFastInspector.postorderCount << std::endl;
    std::cerr << "** Second suspicious preorder (other) count: " << secondFastInspector.otherPreorderCount << std::endl;
    std::cerr << "** Second suspicious postorder (other) count: " << secondFastInspector.otherPostorderCount << std::endl;
}

TEST(FastInspector, NodeClone) {
    auto* node = new IR::Constant(0xff);
    EXPECT_TRUE(node->reachableNodeClassesIsDirty);
    EXPECT_TRUE(node->reachableNodeClasses.none());
    EXPECT_TRUE(node->canReachClass(IR::BAnd::nodeClassId));
    EXPECT_TRUE(node->canReachClass(IR::BOr::nodeClassId));
    EXPECT_TRUE(node->canReachClass(IR::BXor::nodeClassId));

    node->notifyNodeClassIsReachable(IR::BAnd::nodeClassId);
    node->notifyNodeClassIsReachable(IR::BOr::nodeClassId);
    node->markReachableNodeClassesUpToDate();
    EXPECT_FALSE(node->reachableNodeClassesIsDirty);
    EXPECT_FALSE(node->reachableNodeClasses.none());
    EXPECT_TRUE(node->canReachClass(IR::BAnd::nodeClassId));
    EXPECT_TRUE(node->canReachClass(IR::BOr::nodeClassId));
    EXPECT_FALSE(node->canReachClass(IR::BXor::nodeClassId));

    auto* clone = node->clone();
    EXPECT_TRUE(clone->reachableNodeClassesIsDirty);
    EXPECT_TRUE(clone->reachableNodeClasses.none());
    EXPECT_TRUE(clone->canReachClass(IR::BAnd::nodeClassId));
    EXPECT_TRUE(clone->canReachClass(IR::BOr::nodeClassId));
    EXPECT_TRUE(clone->canReachClass(IR::BXor::nodeClassId));
}

TEST(FastInspector, PropagateReachabilityFromUpToDateChildToDirtyParent) {
    struct VisitBOr : public FastInspector<VisitBOr> {
        void postorder(const IR::BOr*) override { }
    };
    VisitBOr visitBOr;

    auto* a = new IR::Constant(0xff);
    auto* b = new IR::BAnd(a, a);

    // When first created, the child nodes should be able to reach anything.
    EXPECT_TRUE(a->canReachClass(IR::BOr::nodeClassId));
    EXPECT_TRUE(a->canReachClass(IR::IfStatement::nodeClassId));
    EXPECT_TRUE(b->canReachClass(IR::BOr::nodeClassId));
    EXPECT_TRUE(b->canReachClass(IR::IfStatement::nodeClassId));

    // Visit the child nodes. This won't match anything, since there's no
    // IR::BOr in this IR tree, but it'll update reachability.
    b->apply(visitBOr);
    EXPECT_TRUE(a->canReachClass(IR::Constant::nodeClassId));
    EXPECT_FALSE(a->canReachClass(IR::BAnd::nodeClassId));
    EXPECT_FALSE(a->canReachClass(IR::BOr::nodeClassId));
    EXPECT_FALSE(a->canReachClass(IR::IfStatement::nodeClassId));
    EXPECT_TRUE(b->canReachClass(IR::Constant::nodeClassId));
    EXPECT_TRUE(b->canReachClass(IR::BAnd::nodeClassId));
    EXPECT_FALSE(b->canReachClass(IR::BOr::nodeClassId));
    EXPECT_FALSE(b->canReachClass(IR::IfStatement::nodeClassId));

    // Wrap the whole thing in another node, and visit it. This again won't
    // match anything, but it will update reachability for `parent`, and it
    // *should* properly record that IR::Constant and IR::Band are reachable via
    // `parent`.
    auto* parent = new IR::LNot(b);
    parent->apply(visitBOr);
    EXPECT_TRUE(parent->canReachClass(IR::Constant::nodeClassId));
    EXPECT_TRUE(parent->canReachClass(IR::BAnd::nodeClassId));
    EXPECT_TRUE(parent->canReachClass(IR::LNot::nodeClassId));
    EXPECT_FALSE(parent->canReachClass(IR::BOr::nodeClassId));
    EXPECT_FALSE(parent->canReachClass(IR::IfStatement::nodeClassId));
}

}  // namespace Test
