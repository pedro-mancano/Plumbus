#ifndef PLUMBUS_CMD_AST_H
#define PLUMBUS_CMD_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef enum {
    CMD_REDIR_IN = 0,       /* < file */
    CMD_REDIR_OUT_TRUNC,    /* > file or N> file */
    CMD_REDIR_OUT_APPEND,   /* >> file or N>> file */
    CMD_REDIR_DUP           /* N>&M */
} cmd_redir_kind_t;

typedef struct cmd_redir {
    cmd_redir_kind_t kind;
    int src_fd;             /* 0=stdin, 1=stdout, 2=stderr */
    int dst_fd;             /* for CMD_REDIR_DUP */
    wchar_t *target;        /* filename for file redirs */
    struct cmd_redir *next;
} cmd_redir_t;

typedef enum {
    CMD_NODE_SIMPLE = 0,
    CMD_NODE_SEQ,           /* & */
    CMD_NODE_AND,           /* && */
    CMD_NODE_OR,            /* || */
    CMD_NODE_PIPE,          /* | */
    CMD_NODE_BLOCK,         /* ( ... ) */
    CMD_NODE_IF,
    CMD_NODE_FOR,
    CMD_NODE_REM
} cmd_node_kind_t;

typedef enum {
    IF_COND_EXIST = 0,
    IF_COND_DEFINED,
    IF_COND_ERRORLEVEL,
    IF_COND_CMDEXTVERSION,
    IF_COND_EQ_STR,         /* == */
    IF_COND_EQU,
    IF_COND_NEQ,
    IF_COND_LSS,
    IF_COND_LEQ,
    IF_COND_GTR,
    IF_COND_GEQ
} cmd_if_kind_t;

typedef enum {
    FOR_KIND_PLAIN = 0,
    FOR_KIND_DIR,           /* /D */
    FOR_KIND_NUM,           /* /L */
    FOR_KIND_REC,           /* /R */
    FOR_KIND_FILE           /* /F */
} cmd_for_kind_t;

typedef struct cmd_node {
    cmd_node_kind_t kind;
    cmd_redir_t *redirs;
    union {
        struct {
            wchar_t *raw_text;  /* Full unescaped command line without redirs or leading @ */
            wchar_t *cmd_word;  /* Extracted command name */
            wchar_t *raw_tail;  /* Text immediately following cmd_word */
            bool suppress_echo; /* Leading @ */
        } simple;
        struct {
            struct cmd_node *left;
            struct cmd_node *right;
        } bin;
        struct {
            struct cmd_node *body;
        } block;
        struct {
            cmd_if_kind_t cond;
            bool case_insensitive;
            bool negated;
            wchar_t *lhs;
            wchar_t *rhs;
            struct cmd_node *then_branch;
            struct cmd_node *else_branch;
        } if_stmt;
        struct {
            cmd_for_kind_t for_kind;
            wchar_t var_char;
            wchar_t *opts_or_root;
            wchar_t *set_spec;
            struct cmd_node *body;
        } for_stmt;
    } u;
} cmd_node_t;

/*
 * Parses a cmd.exe command line into an AST.
 * Returns NULL on empty input or syntax error (err_buf receives error text if syntax error).
 * If out_silent != NULL, *out_silent is true for unclosed '(' at EOF (cmd.exe prints nothing).
 */
cmd_node_t *cmd_ast_parse_ex(const wchar_t *line, wchar_t *err_buf, size_t err_cap, bool *out_silent);
cmd_node_t *cmd_ast_parse(const wchar_t *line, wchar_t *err_buf, size_t err_cap);
void cmd_ast_free(cmd_node_t *node);

#endif /* PLUMBUS_CMD_AST_H */
