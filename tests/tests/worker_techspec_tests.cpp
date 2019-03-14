#include <boost/test/unit_test.hpp>

#include "worker_fixture.hpp"
#include "helpers.hpp"

#include <golos/protocol/worker_operations.hpp>
#include <golos/chain/worker_objects.hpp>

using namespace golos;
using namespace golos::protocol;
using namespace golos::chain;

BOOST_FIXTURE_TEST_SUITE(worker_techspec_tests, worker_fixture)

BOOST_AUTO_TEST_CASE(worker_authorities) {
    BOOST_TEST_MESSAGE("Testing: worker_authorities");

    {
        worker_techspec_operation op;
        op.author = "bob";
        op.permlink = "bob-techspec";
        op.worker_proposal_author = "alice";
        op.worker_proposal_permlink = "alice-proposal";
        op.specification_cost = ASSET_GOLOS(6000);
        op.development_cost = ASSET_GOLOS(60000);
        op.payments_interval = 60;
        op.payments_count = 2;
        CHECK_OP_AUTHS(op, account_name_set(), account_name_set(), account_name_set({"bob"}));
    }

    {
        worker_techspec_delete_operation op;
        op.author = "bob";
        op.permlink = "bob-techspec";
        CHECK_OP_AUTHS(op, account_name_set(), account_name_set(), account_name_set({"bob"}));
    }

    {
        worker_techspec_approve_operation op;
        op.approver = "cyberfounder";
        op.author = "bob";
        op.permlink = "bob-techspec";
        op.state = worker_techspec_approve_state::approve;
        CHECK_OP_AUTHS(op, account_name_set(), account_name_set(), account_name_set({"cyberfounder"}));
    }

    {
        worker_assign_operation op;
        op.assigner = "bob";
        op.worker_techspec_author = "bob";
        op.worker_techspec_permlink = "bob-techspec";
        op.worker = "alice";
        CHECK_OP_AUTHS(op, account_name_set(), account_name_set(), account_name_set({"bob"}));

        op.worker = "";
        CHECK_OP_AUTHS(op, account_name_set(), account_name_set(), account_name_set({"bob"}));
    }
}

BOOST_AUTO_TEST_CASE(worker_techspec_validate) {
    BOOST_TEST_MESSAGE("Testing: worker_techspec_validate");

    BOOST_TEST_MESSAGE("-- Normal case");

    worker_techspec_operation op;
    op.author = "bob";
    op.permlink = "techspec-permlink";
    op.worker_proposal_author = "alice";
    op.worker_proposal_permlink = "proposal-permlink";
    op.specification_cost = ASSET_GOLOS(6000);
    op.development_cost = ASSET_GOLOS(60000);
    op.payments_interval = 60*60*24;
    op.payments_count = 2;
    CHECK_OP_VALID(op);

    BOOST_TEST_MESSAGE("-- Incorrect account or permlink case");

    CHECK_PARAM_INVALID(op, author, "");
    CHECK_PARAM_INVALID(op, permlink, std::string(STEEMIT_MAX_PERMLINK_LENGTH+1, ' '));
    CHECK_PARAM_INVALID(op, worker_proposal_author, "");
    CHECK_PARAM_INVALID(op, worker_proposal_permlink, std::string(STEEMIT_MAX_PERMLINK_LENGTH+1, ' '));

    BOOST_TEST_MESSAGE("-- Non-GOLOS cost case");

    CHECK_PARAM_INVALID(op, specification_cost, ASSET_GBG(6000));
    CHECK_PARAM_INVALID(op, specification_cost, ASSET_GESTS(6000));
    CHECK_PARAM_INVALID(op, development_cost, ASSET_GBG(60000));
    CHECK_PARAM_INVALID(op, development_cost, ASSET_GESTS(60000));

    BOOST_TEST_MESSAGE("-- Negative cost case");

    CHECK_PARAM_INVALID(op, specification_cost, ASSET_GOLOS(-1));
    CHECK_PARAM_INVALID(op, development_cost, ASSET_GOLOS(-1));

    BOOST_TEST_MESSAGE("-- Zero payments count case");

    CHECK_PARAM_INVALID(op, payments_count, 0);

    BOOST_TEST_MESSAGE("-- Too low payments interval case");

    CHECK_PARAM_INVALID(op, payments_interval, 60*60*24 - 1);

    BOOST_TEST_MESSAGE("-- Single payment with too big interval case");

    op.payments_count = 1;
    CHECK_PARAM_INVALID(op, payments_interval, 60*60*24 + 1);

    BOOST_TEST_MESSAGE("-- Single payment with normal interval case");

    op.payments_count = 1;
    CHECK_PARAM_VALID(op, payments_interval, 60*60*24);
}

BOOST_AUTO_TEST_CASE(worker_techspec_apply_create) {
    BOOST_TEST_MESSAGE("Testing: worker_techspec_apply_create");

    ACTORS((alice)(bob)(carol)(dave)(eve)(fred))
    auto private_key = create_approvers(0, STEEMIT_MAJOR_VOTED_WITNESSES);
    generate_block();

    signed_transaction tx;

    BOOST_TEST_MESSAGE("-- Create worker techspec with no post case");

    worker_techspec_operation op;
    op.author = "bob";
    op.permlink = "bob-techspec";
    op.worker_proposal_author = "alice";
    op.worker_proposal_permlink = "alice-proposal";
    op.specification_cost = ASSET_GOLOS(6);
    op.development_cost = ASSET_GOLOS(60);
    op.payments_interval = 60*60*24*2;
    op.payments_count = 2;
    GOLOS_CHECK_ERROR_MISSING(comment, make_comment_id("bob", "bob-techspec"), bob_private_key, op);
    generate_block();

    BOOST_TEST_MESSAGE("-- Create worker techspec on comment instead of post case");

    comment_create("alice", alice_private_key, "alice-proposal", "", "alice-proposal");

    comment_create("carol", carol_private_key, "i-am-comment", "alice", "alice-proposal");

    op.author = "carol";
    op.permlink = "i-am-comment";
    GOLOS_CHECK_ERROR_LOGIC(worker_techspec_can_be_created_only_on_post, carol_private_key, op);
    generate_block();

    BOOST_TEST_MESSAGE("-- Create worker techspec for non-existant proposal");

    comment_create("bob", bob_private_key, "bob-techspec", "", "bob-techspec");

    op.author = "bob";
    op.permlink = "bob-techspec";
    GOLOS_CHECK_ERROR_LOGIC(worker_techspec_can_be_created_only_for_existing_proposal, bob_private_key, op);
    generate_block();

    BOOST_TEST_MESSAGE("-- Create worker techspec for premade_work proposal");

    comment_create("dave", dave_private_key, "dave-proposal", "", "dave-proposal");

    worker_proposal("dave", dave_private_key, "dave-proposal", worker_proposal_type::premade_work);
    generate_block();

    op.worker_proposal_author = "dave";
    op.worker_proposal_permlink = "dave-proposal";
    GOLOS_CHECK_ERROR_LOGIC(cannot_create_techspec_for_premade_worker_proposal, bob_private_key, op);
    generate_block();

    BOOST_TEST_MESSAGE("-- Create worker techspec for worker proposal with approved techspec");

    {
        comment_create("eve", eve_private_key, "eve-proposal", "", "eve-proposal");

        worker_proposal("eve", eve_private_key, "eve-proposal", worker_proposal_type::task);
        generate_block();

        comment_create("fred", fred_private_key, "fred-techspec", "", "fred-techspec");

        op.author = "fred";
        op.permlink = "fred-techspec";
        op.worker_proposal_author = "eve";
        op.worker_proposal_permlink = "eve-proposal";
        BOOST_CHECK_NO_THROW(push_tx_with_ops(tx, fred_private_key, op));

        generate_blocks(STEEMIT_MAX_WITNESSES); // Enough for approvers to reach TOP-19 and not leave it

        for (auto i = 0; i < STEEMIT_MAJOR_VOTED_WITNESSES; ++i) {
            const auto name = "approver" + std::to_string(i);

            worker_techspec_approve_operation wtaop;
            wtaop.approver = name;
            wtaop.author = "fred";
            wtaop.permlink = "fred-techspec";
            wtaop.state = worker_techspec_approve_state::approve;
            BOOST_CHECK_NO_THROW(push_tx_with_ops(tx, private_key, wtaop));
            generate_block();
        }

        op.author = "bob";
        op.permlink = "bob-techspec";
        op.worker_proposal_author = "eve";
        op.worker_proposal_permlink = "eve-proposal";
        GOLOS_CHECK_ERROR_LOGIC(this_worker_proposal_already_has_approved_techspec, bob_private_key, op);
        generate_block();
    }

    BOOST_TEST_MESSAGE("-- Normal create worker techspec case");

    worker_proposal("alice", alice_private_key, "alice-proposal", worker_proposal_type::task);
    generate_block();

    op.worker_proposal_author = "alice";
    op.worker_proposal_permlink = "alice-proposal";
    BOOST_CHECK_NO_THROW(push_tx_with_ops(tx, bob_private_key, op));
    generate_block();

    const auto& wpo_post = db->get_comment("alice", string("alice-proposal"));
    const auto& wto_post = db->get_comment("bob", string("bob-techspec"));
    const auto& wto = db->get_worker_techspec(wto_post.id);
    BOOST_CHECK_EQUAL(wto.post, wto_post.id);
    BOOST_CHECK_EQUAL(wto.worker_proposal_post, wpo_post.id);
    BOOST_CHECK(wto.state == worker_techspec_state::created);
    BOOST_CHECK_EQUAL(wto.specification_cost, op.specification_cost);
    BOOST_CHECK_EQUAL(wto.development_cost, op.development_cost);
    BOOST_CHECK_EQUAL(wto.payments_count, op.payments_count);
    BOOST_CHECK_EQUAL(wto.payments_interval, op.payments_interval);

    BOOST_CHECK_EQUAL(wto.worker, account_name_type());
    BOOST_CHECK_EQUAL(wto.worker_result_post, comment_id_type());
    BOOST_CHECK_EQUAL(wto.next_cashout_time, fc::time_point_sec::maximum());
    BOOST_CHECK_EQUAL(wto.finished_payments_count, 0);

    validate_database();
}

BOOST_AUTO_TEST_SUITE_END()
