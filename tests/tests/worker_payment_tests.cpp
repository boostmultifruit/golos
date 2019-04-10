#include <boost/test/unit_test.hpp>

#include "worker_fixture.hpp"
#include "helpers.hpp"

#include <golos/protocol/worker_operations.hpp>
#include <golos/chain/worker_objects.hpp>

using namespace golos;
using namespace golos::protocol;
using namespace golos::chain;

BOOST_FIXTURE_TEST_SUITE(worker_payment_tests, worker_fixture)

BOOST_AUTO_TEST_CASE(worker_authorities) {
    BOOST_TEST_MESSAGE("Testing: worker_authorities");

    {
        worker_payment_approve_operation op;
        op.approver = "cyberfounder";
        op.worker_techspec_author = "bob";
        op.worker_techspec_permlink = "bob-techspec";
        op.state = worker_techspec_approve_state::approve;
        CHECK_OP_AUTHS(op, account_name_set(), account_name_set(), account_name_set({"cyberfounder"}));
    }
}

BOOST_AUTO_TEST_CASE(worker_payment_approve_validate) {
    BOOST_TEST_MESSAGE("Testing: worker_payment_approve_validate");

    BOOST_TEST_MESSAGE("-- Normal case");

    worker_payment_approve_operation op;
    op.approver = "cyberfounder";
    op.worker_techspec_author = "bob";
    op.worker_techspec_permlink = "bob-techspec";
    op.state = worker_techspec_approve_state::approve;
    CHECK_OP_VALID(op);

    BOOST_TEST_MESSAGE("-- Incorrect account or permlink case");

    CHECK_PARAM_INVALID(op, approver, "");
    CHECK_PARAM_INVALID(op, worker_techspec_author, "");
    CHECK_PARAM_INVALID(op, worker_techspec_permlink, std::string(STEEMIT_MAX_PERMLINK_LENGTH+1, ' '));

    BOOST_TEST_MESSAGE("-- Invalid state case");

    CHECK_PARAM_INVALID(op, state, worker_techspec_approve_state::_size);
}

BOOST_AUTO_TEST_CASE(worker_payment_approve_apply) {
    BOOST_TEST_MESSAGE("Testing: worker_payment_approve_apply_approve");

    ACTORS((alice)(bob))
    auto private_key = create_approvers(0, STEEMIT_MAJOR_VOTED_WITNESSES);
    generate_block();

    signed_transaction tx;

    comment_create("alice", alice_private_key, "alice-proposal", "", "alice-proposal");

    worker_proposal("alice", alice_private_key, "alice-proposal", worker_proposal_type::task);
    generate_block();

    BOOST_TEST_MESSAGE("-- Approving payment by not witness case");

    worker_payment_approve_operation op;
    op.approver = "alice";
    op.worker_techspec_author = "bob";
    op.worker_techspec_permlink = "bob-techspec";
    op.state = worker_techspec_approve_state::approve;
    GOLOS_CHECK_ERROR_MISSING(witness, "alice", alice_private_key, op);

    BOOST_TEST_MESSAGE("-- Approving payment by witness not in TOP-19 case");

    op.approver = "approver0";
    GOLOS_CHECK_ERROR_LOGIC(approver_of_payment_should_be_in_top19_of_witnesses, private_key, op);

    generate_blocks(STEEMIT_MAX_WITNESSES); // Enough for approvers to reach TOP-19 and not leave it

    BOOST_TEST_MESSAGE("-- Approving payment without techspec post case");

    GOLOS_CHECK_ERROR_MISSING(comment, make_comment_id("bob", "bob-techspec"), private_key, op);

    BOOST_TEST_MESSAGE("-- Approving payment for non-existing techspec case");

    comment_create("bob", bob_private_key, "bob-techspec", "", "bob-techspec");

    GOLOS_CHECK_ERROR_MISSING(worker_techspec_object, make_comment_id("bob", "bob-techspec"), private_key, op);

    BOOST_TEST_MESSAGE("-- Creating techspec and approving it");

    worker_techspec_operation wtop;
    wtop.author = "bob";
    wtop.permlink = "bob-techspec";
    wtop.worker_proposal_author = "alice";
    wtop.worker_proposal_permlink = "alice-proposal";
    wtop.specification_cost = ASSET_GOLOS(6);
    wtop.development_cost = ASSET_GOLOS(60);
    wtop.payments_interval = 60*60*24;
    wtop.payments_count = 40;
    BOOST_CHECK_NO_THROW(push_tx_with_ops(tx, bob_private_key, wtop));
    generate_block();

    for (auto i = 0; i < STEEMIT_MAJOR_VOTED_WITNESSES; ++i) {
        worker_techspec_approve("approver" + std::to_string(i), private_key,
            "bob", "bob-techspec", worker_techspec_approve_state::approve);
        generate_block();
    }

    BOOST_TEST_MESSAGE("-- Approving payment before work started");

    GOLOS_CHECK_ERROR_LOGIC(worker_techspec_should_be_in_work_complete_or_paying, private_key, op);

    BOOST_TEST_MESSAGE("-- Approving payment in techspec work state");

    worker_assign("bob", bob_private_key, "bob", "bob-techspec", "alice");

    GOLOS_CHECK_ERROR_LOGIC(techspec_cannot_be_approved_when_paying_or_not_finished, private_key, op);

    BOOST_TEST_MESSAGE("-- Approving payment in techspec complete state");

    generate_blocks(60 / STEEMIT_BLOCK_INTERVAL); // Waiting for posts window

    comment_create("bob", bob_private_key, "bob-result", "", "bob-result");
    worker_result("bob", bob_private_key, "bob-result", "bob-techspec");

    auto check_approves = [&](int approve_count, int disapprove_count) {
        auto approves = db->count_worker_payment_approves(db->get_comment("bob", string("bob-techspec")).id);
        BOOST_CHECK_EQUAL(approves[worker_techspec_approve_state::approve], approve_count);
        BOOST_CHECK_EQUAL(approves[worker_techspec_approve_state::disapprove], disapprove_count);
    };

    check_approves(0, 0);

    worker_payment_approve("approver0", private_key,
        "bob", "bob-techspec", worker_techspec_approve_state::approve);
    generate_block();

    check_approves(1, 0);

    for (auto i = 1; i < STEEMIT_MAJOR_VOTED_WITNESSES - 1; ++i) {
        worker_payment_approve("approver" + std::to_string(i), private_key,
            "bob", "bob-techspec", worker_techspec_approve_state::approve);
        generate_block();
    }

    check_approves(STEEMIT_MAJOR_VOTED_WITNESSES - 1, 0);

    {
        const auto& wto = db->get_worker_techspec(db->get_comment("bob", string("bob-techspec")).id);
        BOOST_CHECK(wto.state != worker_techspec_state::payment);
        BOOST_CHECK_EQUAL(wto.next_cashout_time, time_point_sec::maximum());
    }

    const auto now = db->head_block_time();

    worker_payment_approve("approver" + std::to_string(STEEMIT_MAJOR_VOTED_WITNESSES - 1), private_key,
        "bob", "bob-techspec", worker_techspec_approve_state::approve);
    generate_block();

    check_approves(STEEMIT_MAJOR_VOTED_WITNESSES, 0);

    {
        const auto& wto = db->get_worker_techspec(db->get_comment("bob", string("bob-techspec")).id);
        BOOST_CHECK(wto.state == worker_techspec_state::payment);
        BOOST_CHECK_EQUAL(wto.next_cashout_time, now + wto.payments_interval);
    }

    BOOST_TEST_MESSAGE("-- Approving payment in techspec payment state");

    GOLOS_CHECK_ERROR_LOGIC(techspec_cannot_be_approved_when_paying_or_not_finished, private_key, op);
}

BOOST_AUTO_TEST_CASE(worker_payment_disapprove) {
    BOOST_TEST_MESSAGE("Testing: worker_payment_disapprove");

    ACTORS((alice)(bob)(carol))
    auto private_key = create_approvers(0, STEEMIT_SUPER_MAJOR_VOTED_WITNESSES);
    generate_block();

    signed_transaction tx;

    comment_create("alice", alice_private_key, "alice-proposal", "", "alice-proposal");

    worker_proposal("alice", alice_private_key, "alice-proposal", worker_proposal_type::task);
    generate_block();

    BOOST_TEST_MESSAGE("-- Creating 2 techspecs (bob's will be disapproved before payment, carol's - on payment)");

    comment_create("bob", bob_private_key, "bob-techspec", "", "bob-techspec");

    worker_techspec_operation wtop;
    wtop.author = "bob";
    wtop.permlink = "bob-techspec";
    wtop.worker_proposal_author = "alice";
    wtop.worker_proposal_permlink = "alice-proposal";
    wtop.specification_cost = ASSET_GOLOS(6);
    wtop.development_cost = ASSET_GOLOS(60);
    wtop.payments_interval = 60*60*24;
    wtop.payments_count = 40;
    BOOST_CHECK_NO_THROW(push_tx_with_ops(tx, bob_private_key, wtop));
    generate_block();

    comment_create("carol", carol_private_key, "carol-techspec", "", "carol-techspec");

    wtop.author = "carol";
    wtop.permlink = "carol-techspec";
    BOOST_CHECK_NO_THROW(push_tx_with_ops(tx, carol_private_key, wtop));
    generate_block();

    BOOST_TEST_MESSAGE("-- Working with bob techspec");

    generate_blocks(STEEMIT_MAX_WITNESSES); // Enough for approvers to reach TOP-19 and not leave it

    for (auto i = 0; i < STEEMIT_MAJOR_VOTED_WITNESSES; ++i) {
        worker_techspec_approve("approver" + std::to_string(i), private_key,
            "bob", "bob-techspec", worker_techspec_approve_state::approve);
        generate_block();
    }

    worker_assign("bob", bob_private_key, "bob", "bob-techspec", "alice");

    BOOST_TEST_MESSAGE("---- Disapproving work");

    auto check_approves = [&](const std::string& author, const std::string& permlink, int approve_count, int disapprove_count) {
        auto approves = db->count_worker_payment_approves(db->get_comment(author, permlink).id);
        BOOST_CHECK_EQUAL(approves[worker_techspec_approve_state::approve], approve_count);
        BOOST_CHECK_EQUAL(approves[worker_techspec_approve_state::disapprove], disapprove_count);
    };

    check_approves("bob", "bob-techspec", 0, 0);

    worker_payment_approve("approver0", private_key,
        "bob", "bob-techspec", worker_techspec_approve_state::disapprove);
    generate_block();

    check_approves("bob", "bob-techspec", 0, 1);

    for (auto i = 1; i < STEEMIT_SUPER_MAJOR_VOTED_WITNESSES; ++i) {
        worker_payment_approve("approver" + std::to_string(i), private_key,
            "bob", "bob-techspec", worker_techspec_approve_state::disapprove);
        generate_block();
    }

    check_approves("bob", "bob-techspec", 0, STEEMIT_SUPER_MAJOR_VOTED_WITNESSES);

    BOOST_TEST_MESSAGE("-- Checking bob techspec is closed");

    {
        const auto& wto = db->get_worker_techspec(db->get_comment("bob", string("bob-techspec")).id);
        BOOST_CHECK(wto.state == worker_techspec_state::closed_by_witnesses);

        const auto& wpo = db->get_worker_proposal(wto.worker_proposal_post);
        BOOST_CHECK(wpo.state == worker_proposal_state::created);
        BOOST_CHECK_EQUAL(wpo.approved_techspec_post, comment_id_type(-1));

        const auto& gpo = db->get_dynamic_global_properties();
        BOOST_CHECK_EQUAL(gpo.worker_consumption_per_day.amount, 0);
    }

    BOOST_TEST_MESSAGE("-- Working with carol techspec");

    for (auto i = 0; i < STEEMIT_MAJOR_VOTED_WITNESSES; ++i) {
        worker_techspec_approve("approver" + std::to_string(i), private_key,
            "carol", "carol-techspec", worker_techspec_approve_state::approve);
        generate_block();
    }

    worker_assign("carol", carol_private_key, "carol", "carol-techspec", "alice");

    BOOST_TEST_MESSAGE("---- Disapproving work by 1 witness");

    worker_payment_approve("approver0", private_key,
        "carol", "carol-techspec", worker_techspec_approve_state::disapprove);
    generate_block();

    check_approves("carol", "carol-techspec", 0, 1);

    BOOST_TEST_MESSAGE("---- Publishing result");

    generate_blocks(60 / STEEMIT_BLOCK_INTERVAL); // Waiting for posts window

    comment_create("carol", carol_private_key, "carol-result", "", "carol-result");
    worker_result("carol", carol_private_key, "carol-result", "carol-techspec");

    BOOST_TEST_MESSAGE("---- Disapproving result by 1 witness");

    worker_payment_approve("approver1", private_key,
        "carol", "carol-techspec", worker_techspec_approve_state::disapprove);
    generate_block();

    check_approves("carol", "carol-techspec", 0, 2);

    BOOST_TEST_MESSAGE("---- Setting state to wip");

    worker_result_delete_operation wrdop;
    wrdop.author = "carol";
    wrdop.permlink = "carol-result";
    BOOST_CHECK_NO_THROW(push_tx_with_ops(tx, carol_private_key, wrdop));
    generate_block();

    BOOST_TEST_MESSAGE("---- Disapproving wip by 1 witness");

    worker_payment_approve("approver2", private_key,
        "carol", "carol-techspec", worker_techspec_approve_state::disapprove);
    generate_block();

    check_approves("carol", "carol-techspec", 0, 3);

    BOOST_TEST_MESSAGE("---- Publishing result again");

    worker_result("carol", carol_private_key, "carol-result", "carol-techspec");

    BOOST_TEST_MESSAGE("---- Approving result by enough witnesses");

    for (auto i = 3; i < STEEMIT_MAJOR_VOTED_WITNESSES + 3; ++i) {
        worker_payment_approve("approver" + std::to_string(i), private_key,
            "carol", "carol-techspec", worker_techspec_approve_state::approve);
        generate_block();
    }

    check_approves("carol", "carol-techspec", STEEMIT_MAJOR_VOTED_WITNESSES, 3);

    BOOST_TEST_MESSAGE("---- Disapproving payment by enough witnesses");

    for (auto i = 3; i < STEEMIT_SUPER_MAJOR_VOTED_WITNESSES; ++i) {
        worker_payment_approve("approver" + std::to_string(i), private_key,
            "carol", "carol-techspec", worker_techspec_approve_state::disapprove);
        generate_block();
    }

    check_approves("carol", "carol-techspec", 0, STEEMIT_SUPER_MAJOR_VOTED_WITNESSES);

    BOOST_TEST_MESSAGE("-- Checking carol techspec is closed");

    {
        const auto& wto = db->get_worker_techspec(db->get_comment("carol", string("carol-techspec")).id);
        BOOST_CHECK(wto.state == worker_techspec_state::disapproved_by_witnesses);

        const auto& wpo = db->get_worker_proposal(wto.worker_proposal_post);
        BOOST_CHECK(wpo.state == worker_proposal_state::created);
        BOOST_CHECK_EQUAL(wpo.approved_techspec_post, comment_id_type(-1));

        const auto& gpo = db->get_dynamic_global_properties();
        BOOST_CHECK_EQUAL(gpo.worker_consumption_per_day.amount, 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()