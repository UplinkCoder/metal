#ifndef _METAC_PARSER_H_
#define _METAC_PARSER_H_

#ifndef ACCEL
#  error "You must compile the parser with ACCEL set"
#  error "Known values are ACCEL_TABLE"
#else
#  if ACCEL == ACCEL_TABLE
#    include "metac_identifier_table.h"
#  else
#    error "Unknow ACCEL value " #ACCEL
#  endif

extern const void* _emptyPointer;
#define emptyPointer ((void*)_emptyPointer)

#include "compat.h"
#include "metac_lexer.h"
#include "metac_parsetree.h"

#ifdef IDENTIFIER_TABLE
#  include "metac_identifier_table.h"
#endif

#include "metac_dot_printer.h"

#ifdef IDENTIFIER_TREE
#  include "metac_identifier_tree.h"
#endif
/*    M(exp_bin_invalid) \*/

typedef enum parse_expression_flags_t
{
    expr_flags_none,
    expr_flags_call = (1 << 0),
    expr_flags_unary = (1 << 1),
} parse_expression_flags_t;


typedef struct metac_define_t
{
    uint32_t IdentifierKey;
    metac_identifier_ptr_t IdentifierPtr;

    /// at which tokden positon the define is
    uint32_t TokenPosition;
    uint32_t SourceId;
    ///UINT32_MAX means variadic
    uint32_t NumberOfParameters;
} metac_define_t;

typedef struct metac_parser_t
{
    metac_lexer_t* Lexer;
    metac_lexer_state_t* LexerState;

    uint32_t CurrentTokenIndex;
    metac_identifier_table_t IdentifierTable;
    metac_identifier_table_t StringTable;

    metac_define_t* Defines;
    uint32_t DefineCount;
    uint32_t DefineCapacity;

    metac_dot_printer_t* DotPrinter;

    metac_define_t inlineDefines[8];

} metac_parser_t;

extern metac_parser_t g_lineParser;
bool IsBinaryAssignExp(metac_expression_kind_t exp_kind);

void MetaCParser_InitFromLexer(metac_parser_t* self, metac_lexer_t* lexer);
metac_expression_t* MetaCParser_ParseExpression(metac_parser_t* self, parse_expression_flags_t flags, metac_expression_t* prev);
metac_expression_t* MetaCParser_ParseExpressionFromString(const char* exp);
metac_declaration_t* MetaCParser_ParseDeclaration(metac_parser_t* self, metac_declaration_t* parent);
#endif // ifndef ACCEL
#endif
