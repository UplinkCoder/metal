/// lpp stands for lexer, preprocessor, parser
/// and it's a struct to bundle those together
#ifndef _METAC_LPP_H_
#define _METAC_LPP_H_

#include "metac_lexer.h"
#include "metac_parser.h"

#ifndef NO_PREPROCESSOR
#  include "metac_preproc.h"
#endif

typedef struct metac_lpp_t
{
    metac_lexer_state_t LexerState;
    metac_lexer_t Lexer;
    metac_parser_t Parser;
#ifndef NO_PREPROCESSOR
    metac_preprocessor_t Preprocessor;
#endif
} metac_lpp_t;

void MetaCLPP_Init(metac_lpp_t*);

metac_expression_t* MetaCLPP_ParseExpressionFromString(metac_lpp_t* lpp, const char* exp);

metac_statement_t* MetaCLPP_ParseStatementFromString(metac_lpp_t* lpp, const char* stmt);

metac_declaration_t* MetaCLPP_ParseDeclarationFromString(metac_lpp_t* lpp, const char* decl);

#ifndef NO_PREPROCESSOR
metac_preprocessor_directive_t MetaCLPP_ParsePreprocFromString(metac_lpp_t* lpp, const char* line,
                                                               metac_token_buffer_t* tokenBuffer);
#endif

#endif
