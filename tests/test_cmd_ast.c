#include "test_framework.h"
#include "cmd_ast.h"

TEST_CASE(test_ast_simple_quotes_and_escapes) {
    wchar_t err[256];
    /* Unclosed quotes in echo must parse as valid simple node */
    cmd_node_t *n1 = cmd_ast_parse(L"echo \"a'", err, 256);
    ASSERT_TRUE(n1 != NULL);
    ASSERT_EQ_INT(CMD_NODE_SIMPLE, n1->kind);
    ASSERT_EQ_WSTR(L"echo", n1->u.simple.cmd_word);
    cmd_ast_free(n1);

    cmd_node_t *n2 = cmd_ast_parse(L"echo a\"", err, 256);
    ASSERT_TRUE(n2 != NULL);
    ASSERT_EQ_INT(CMD_NODE_SIMPLE, n2->kind);
    cmd_ast_free(n2);

    /* Caret escape & */
    cmd_node_t *n3 = cmd_ast_parse(L"echo a ^& b", err, 256);
    ASSERT_TRUE(n3 != NULL);
    ASSERT_EQ_INT(CMD_NODE_SIMPLE, n3->kind);
    ASSERT_EQ_WSTR(L" a & b", n3->u.simple.raw_tail);
    cmd_ast_free(n3);

    /* Unbalanced paren must fail */
    cmd_node_t *n4 = cmd_ast_parse(L"(echo broken", err, 256);
    ASSERT_TRUE(n4 == NULL);
    ASSERT_TRUE(err[0] != L'\0');
}

TEST_CASE(test_ast_operators_redirs_and_comments) {
    wchar_t err[256];
    cmd_node_t *seq = cmd_ast_parse(L"echo 1 && echo 2 | findstr 2 > out.txt 2>&1", err, 256);
    ASSERT_TRUE(seq != NULL);
    ASSERT_EQ_INT(CMD_NODE_AND, seq->kind);
    ASSERT_EQ_INT(CMD_NODE_PIPE, seq->u.bin.right->kind);
    cmd_node_t *rhs = seq->u.bin.right->u.bin.right;
    ASSERT_EQ_INT(CMD_NODE_SIMPLE, rhs->kind);
    ASSERT_TRUE(rhs->redirs != NULL);
    ASSERT_EQ_INT(CMD_REDIR_OUT_TRUNC, rhs->redirs->kind);
    ASSERT_EQ_WSTR(L"out.txt", rhs->redirs->target);
    ASSERT_TRUE(rhs->redirs->next != NULL);
    ASSERT_EQ_INT(CMD_REDIR_DUP, rhs->redirs->next->kind);
    cmd_ast_free(seq);

    cmd_node_t *rem = cmd_ast_parse(L"rem comment & echo LEAK", err, 256);
    ASSERT_TRUE(rem != NULL);
    ASSERT_EQ_INT(CMD_NODE_REM, rem->kind);
    cmd_ast_free(rem);
}

TEST_CASE(test_ast_if_and_for) {
    wchar_t err[256];
    cmd_node_t *ifn = cmd_ast_parse(L"if /i not \"a\"==\"b\" (echo yes) else (echo no)", err, 256);
    ASSERT_TRUE(ifn != NULL);
    ASSERT_EQ_INT(CMD_NODE_IF, ifn->kind);
    ASSERT_TRUE(ifn->u.if_stmt.case_insensitive);
    ASSERT_TRUE(ifn->u.if_stmt.negated);
    ASSERT_TRUE(ifn->u.if_stmt.then_branch != NULL);
    ASSERT_TRUE(ifn->u.if_stmt.else_branch != NULL);
    cmd_ast_free(ifn);

    cmd_node_t *forn = cmd_ast_parse(L"for /L %i in (1,1,5) do @echo %i", err, 256);
    ASSERT_TRUE(forn != NULL);
    ASSERT_EQ_INT(CMD_NODE_FOR, forn->kind);
    ASSERT_EQ_INT(FOR_KIND_NUM, forn->u.for_stmt.for_kind);
    ASSERT_EQ_INT(L'i', forn->u.for_stmt.var_char);
    ASSERT_EQ_WSTR(L"1,1,5", forn->u.for_stmt.set_spec);
    cmd_ast_free(forn);
}

int wmain(void) {
    RUN_TEST(test_ast_simple_quotes_and_escapes);
    RUN_TEST(test_ast_operators_redirs_and_comments);
    RUN_TEST(test_ast_if_and_for);
    TEST_SUMMARY();
}
