#include "compat.h"
#include "metac_lexer.h"
#include "metac_identifier_table.h"

/*    M(exp_bin_invalid) \*/

#define FIRST_BINARY_EXP(M) \
    M(exp_comma)

#define LAST_BINARY_EXP(M) \
    M(exp_spaceship)

#define FOREACH_DECL_KIND(M) \
    M(decl_variable) \
    M(decl_struct) \
    M(decl_enum) \
    M(decl_function) \
    M(decl_typedef)

#define FOREACH_BINARY_EXP(M) \
    FIRST_BINARY_EXP(M) \
    FOREACH_BINARY_EXP_(M) \
    LAST_BINARY_EXP(M)

#define FOREACH_BINARY_EXP_(M) \
    M(exp_dot) \
    \
    M(exp_add) \
    M(exp_sub) \
    M(exp_mul) \
    M(exp_div) \
    M(exp_xor) \
    M(exp_or) \
    M(exp_and) \
    M(exp_cat) \
    M(exp_lsh) \
    M(exp_rsh) \
    \
    M(exp_oror) \
    M(exp_andand) \
    \
    M(exp_arrow) \
    M(exp_dotdot) \
    \
    M(exp_assign) \
    \
    M(exp_add_ass) \
    M(exp_sub_ass) \
    M(exp_mul_ass) \
    M(exp_div_ass) \
    M(exp_xor_ass) \
    M(exp_or_ass) \
    M(exp_and_ass) \
    M(exp_cat_ass) \
    M(exp_lsh_ass) \
    M(exp_rsh_ass) \
    \
    M(exp_eq) \
    M(exp_neq) \
    M(exp_lt) \
    M(exp_le) \
    M(exp_gt) \
    M(exp_ge)

#define FOREACH_EXP(M) \
    M(exp_invalid) \
    \
    M(exp_identifier) \
    M(exp_string) \
    M(exp_signed_integer) \
    M(exp_increment) \
    M(exp_decrement) \
    M(exp_post_increment) \
    M(exp_post_decrement) \
    M(exp_typeof) \
    M(exp_inject) \
    M(exp_eject) \
    M(exp_assert) \
    M(exp_outer) \
    M(exp_addr) \
    M(exp_ptr) \
    M(exp_paren) \
    \
    FOREACH_BINARY_EXP(M) \
    \
    M(exp_full_slice) \
    M(exp_slice) \
    M(exp_call) \
    \
    M(exp_addr_or_and) \
    M(exp_ptr_or_mul) \
    \
    M(exp_max)


#define FOREACH_STMT_KIND(M) \
    M(stmt_min) \
    \
    FOREACH_STMT_KIND_(M) \
    \
    M(stmt_max)

#define FOREACH_STMT_KIND_(M) \
    M(stmt_block) \
    M(stmt_if) \
    M(stmt_switch) \
    M(stmt_while) \
    M(stmt_label) \
    M(stmt_case) \
    M(stmt_break) \
    M(stmt_yield) \
    M(stmt_scope) \
    M(stmt_continue) \
    M(stmt_goto) \
    \
    M(stmt_exp) \
    M(stmt_decl)


#define FOREACH_NDOE_KIND(M) \
    FOREACH_EXP(M) \
    FOREACH_STMT_KIND(M) \
    FOREACH_DECL_KIND(M) \
    M(node_max)

#define DEFINE_MEMBERS(MEMBER) \
    MEMBER,

typedef enum metac_declaration_kind_t
{
    FOREACH_DECL_KIND(DEFINE_MEMBERS)
} metac_declaration_kind_t;

typedef enum metac_expression_kind_t
{
    FOREACH_EXP(DEFINE_MEMBERS)
} metac_expression_kind_t;


#define BIN_MEMBERS(MEMB) \
    bin_ ## MEMB,

typedef enum metac_binary_expression_kind_t
{
    bin_exp_min = (FIRST_BINARY_EXP(TOK_SELF) - 1),

    FOREACH_BINARY_EXP(BIN_MEMBERS)
} metac_binary_expression_kind_t;

typedef struct metac_expression_t
{
    metac_expression_kind_t Kind;
    uint32_t LocationIdx;
    uint32_t Hash;

    union // switch(Kind)
    {
        // invalid case exp_max, exp_invalid :

        // case exp_add, exp_sub, exp_mul, exp_div, exp_cat, exp_catAss, exp_assign,
        // exp_lt, exp_gt, exp_le, exp_ge, exp_spaceShip :
        struct {
            struct metac_expression_t* _E1;
            struct metac_expression_t* E2;
        };
        // case  exp_inject, exp_eject, exp_assert, exp_outerParen, exp_outer :
        struct {
            struct metac_expression_t* E1;
        };
        // case identifier_exp :
        struct {
            uint32_t IdentifierKey;
            metac_identifier_ptr_t IdentifierPtr;
        };
        // case exp_string :
        struct {
            uint32_t StringKey;
            const char* String;
        };

        // case exp_signed_integer :
        int64_t ValueI64;
        // case exp_unsigned_integer :
        uint64_t ValueU64;
    };
} metac_expression_t;

typedef enum metac_statement_kind_t
{
    stmt_min = exp_max + 1,

    FOREACH_STMT_KIND_(DEFINE_MEMBERS)

    stmt_max
} metac_statement_kind_t;

#define STATEMENT_HEADER \
    metac_statement_kind_t Kind; \
    uint32_t LocationIdx; \
    uint32_t Hash; \
    struct metac_statement_t* Next;

typedef struct stmt_block_t
{
    STATEMENT_HEADER
} stmt_block_t;

typedef struct stmt_case_t
{
    STATEMENT_HEADER

    metac_expression_t* E1;
} stmt_case_t;

typedef struct stmt_goto_t
{
    STATEMENT_HEADER

    metac_identifier_ptr_t Label;
} stmt_goto_t;

typedef struct stmt_exp_t
{
    STATEMENT_HEADER

    metac_expression_t* Expression;
} stmt_exp_t;

typedef struct stmt_if_t
{
    STATEMENT_HEADER

    metac_expression_t* IfCond;
    struct metac_statement_t* IfBody;
    struct metac_statement_t* ElseBody;
} stmt_if_t;

typedef struct stmt_label_t
{
    STATEMENT_HEADER

    metac_identifier_ptr_t Label;
} stmt_label_t;

typedef struct stmt_switch_t
{
    STATEMENT_HEADER

    metac_identifier_ptr_t Label;
} stmt_switch_t;

typedef struct metac_statement_t
{
    STATEMENT_HEADER

    union // switch(Kind)
    {
        // invalid case stmt_max, stmt_invalid :
        // case stmt_if :
        stmt_if_t stmt_if;
        // case stmt_exp :
        stmt_exp_t stmt_exp;
        // case stmt_block :
        stmt_block_t stmt_block;
        // case stmt_label :
        stmt_label_t stmt_label;
        // case stmt_goto :
        stmt_goto_t stmt_goto;
        // case stmt_yield :
    };
} metac_statement_t;

#define DECLARATION_HEADER \
    metac_declaration_kind_t Kind; \
    uint32_t LocationIdx; \
    uint32_t Hash;

typedef struct metac_declaration_t
{
    DECLARATION_HEADER


} metac_declaration_t;

typedef struct metac_parser_reorder_state_t
{
    metac_expression_t* operandStack[1024];
    metac_expression_kind_t operatorStack[1024];
    uint32_t nOperands;
    uint32_t nOperators;
    uint32_t Depth;
} metac_parser_reorder_state_t;

typedef struct metac_define_t
{
    metac_identifier_ptr_t IdentifierPtr;

    /// at which tokden positon the define is
    uint32_t TokenPosition;
    ///UINT32_MAX means variadic
    uint32_t NumberOfParameters
} metac_define_t;

typedef struct metac_parser_t
{
    metac_lexer_t* Lexer;
    metac_lexer_state_t* LexerState;

    uint32_t CurrentTokenIndex;
    metac_parser_reorder_state_t* ExpressionReorderState;

    metac_identifier_table_t IdentifierTable;

    metac_define_t* Defines;
    uint32_t DefineCount;
    uint32_t DefineCapacity;

    metac_define_t inlineDefines[8];
} metac_parser_t;

extern metac_parser_t g_lineParser;

void MetaCParserInitFromLexer(metac_parser_t* self, metac_lexer_t* lexer);
metac_expression_t* MetaCParserParseExpression(metac_parser_t* self, metac_expression_t* prev);
metac_expression_t* MetaCParserParseExpressionFromString(const char* exp);
const char* PrintExpression(metac_parser_t* self, metac_expression_t* exp);
#undef DEFINE_MEMBERS
