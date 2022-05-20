#ifndef _METAC_PARSER_C_
#define _METAC_PARSER_C_

#ifndef ACCEL
#  error "You must compile the parser with ACCEL set"
#  error "Known values are ACCEL_TABLE and ACCEL_TREE"
#else
#  if ACCEL == ACCEL_TABLE
#    include "metac_identifier_table.c"
#  elif ACCEL == ACCEL_TREE
#    include "metac_identifier_tree.c"
#  else
#    error "Unknow ACCEL value "
#    define DO_NOT_COMPILE
#  endif
#endif

#ifndef DO_NOT_COMPILE

#include "metac_lexer.c"
#include "metac_parser.h"
#include "metac_alloc_node.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "3rd_party/tracy/TracyC.h"

const void* _emptyPointer = (const void*)0x1;

#ifndef emptyPointer
#  define emptyPointer ((void*)_emptyPointer)
#endif

void _newMemRealloc(void** memP, uint32_t* capacity, const uint32_t elementSize);
const char* MetaCExpressionKind_toChars(metac_expression_kind_t type);
#define ARRAY_SIZE(A) \
     ((unsigned int)(sizeof((A)) / sizeof((A)[0])))

bool IsExpressionNode(metac_node_kind_t Kind)
{
    return ((Kind > node_exp_invalid) & (Kind < node_exp_max));
}

void MetaCParser_Init(metac_parser_t* self)
{
    self->CurrentTokenIndex = 0;
    IdentifierTableInit(&self->IdentifierTable);
    IdentifierTableInit(&self->StringTable);
    self->Defines = self->inlineDefines;
    self->DefineCount = 0;
    self->DefineCapacity = ARRAY_SIZE(self->inlineDefines);
    self->LexerState = 0;

    self->BlockStatementStackCapacity = 16;
    self->BlockStatementStackSize = 0;
    self->BlockStatementStack = (stmt_block_t**)
        malloc(sizeof(stmt_block_t**) * self->BlockStatementStackCapacity);

    self->OpenParens = 0;
    self->SpecialNamePtr_Compiler.v = 0;

#ifndef NO_DOT_PRINTER
    self->DotPrinter = (metac_dot_printer_t*)malloc(sizeof(metac_dot_printer_t));
    MetaCDotPrinter_Init(self->DotPrinter, &self->IdentifierTable);
#endif
}

void MetaCParser_InitFromLexer(metac_parser_t* self, metac_lexer_t* lexer)
{
    self->Lexer = lexer;
    MetaCParser_Init(self);
}
//TODO Implement IsMacro
//    and Handle Macro
#define HandleMacro(...)
#define HandlePreprocessor(...)
#define IsMacro(...) false

metac_identifier_ptr_t RegisterIdentifier(metac_parser_t* self,
                                          metac_token_t* token)
{
    const char* identifierString =
        IdentifierPtrToCharPtr(
            MEMBER_SUFFIX(&self->Lexer->Identifier),
            token->IdentifierPtr
        );

    uint32_t identifierKey = token->IdentifierKey;
    return GetOrAddIdentifier(MEMBER_SUFFIX(&self->Identifier),
                              identifierKey, identifierString,
                              LENGTH_FROM_IDENTIFIER_KEY(identifierKey));
}

metac_identifier_ptr_t RegisterString(metac_parser_t* self,
                                      metac_token_t* token)
{
    const char* string =
        IdentifierPtrToCharPtr(
            MEMBER_SUFFIX(&self->Lexer->String),
            token->StringPtr
        );
        uint32_t stringKey = token->StringKey;
        return GetOrAddIdentifier(MEMBER_SUFFIX(&self->String),
                                  stringKey, string,
                                  LENGTH_FROM_STRING_KEY(stringKey));
}

void AddDefine(metac_parser_t* self, metac_token_t* token, uint32_t nParameters)
{
    metac_define_t define;

    assert(token->TokenType == tok_identifier);

    define.NumberOfParameters = nParameters;
    define.IdentifierPtr = RegisterIdentifier(self, token);
    define.TokenPosition = token->Position;
    define.SourceId = self->LexerState->SourceId;
    define.IdentifierKey = token->IdentifierKey;

    assert(self->DefineCount < self->DefineCapacity);
    self->Defines[self->DefineCount++] = define;

    if (self->DefineCapacity >= self->DefineCount)
    {
        bool wasInline = (self->Defines == self->inlineDefines);
        if (wasInline)
        {
            self->Defines = (metac_define_t*)malloc(32 * sizeof(metac_define_t));
            self->DefineCapacity = 32;
            memcpy(self->Defines, self->inlineDefines,
                sizeof(metac_define_t) * ARRAY_SIZE(self->inlineDefines));
            return ;
        }
        _newMemRealloc((void**)&self->Defines, &self->DefineCapacity, sizeof(metac_define_t));

    }
}

metac_token_t* MetaCParser_NextToken(metac_parser_t* self)
{
#define define_key 0x6a491b
#define ifdef_key 0x581ce0
#define endif_key 0x506843

#define NextToken() \
    ((self->CurrentTokenIndex < self->Lexer->TokenSize) ? \
    self->Lexer->Tokens + self->CurrentTokenIndex++ : 0)

#define PeekMatch(TOKEN_TYPE) \
    ( ((result = NextToken()), (result && result->TokenType == TOKEN_TYPE)) ? \
    ( result ) : ( self->CurrentTokenIndex--, (metac_token_t*)0) )

    metac_token_t* result = 0;
    assert(self->Lexer->TokenSize);

    result = NextToken();
    if(result)
    {
        if (result->TokenType == tok_identifier)
        {
            metac_define_t* matchingDefine = 0;

            const uint32_t idKey = result->IdentifierKey;
            for(int defineIdx = 0;
                defineIdx < self->DefineCount;
                defineIdx++)
            {
                metac_define_t* define = self->Defines + defineIdx;

                if (define->IdentifierKey == idKey)
                {
                    const char* defineString =
                        IDENTIFIER_PTR(MEMBER_SUFFIX(&self->Identifier), *define);
                    const char* IdString =
                        IDENTIFIER_PTR(MEMBER_SUFFIX(&self->Lexer->Identifier), *result);

                    if (!memcmp(IdString, defineString,
                        LENGTH_FROM_IDENTIFIER_KEY(idKey)))
                    {
                        matchingDefine = define;
                        break;
                    }
                }
            }

            if (matchingDefine)
            {
                const char* defineName =
                        IDENTIFIER_PTR(MEMBER_SUFFIX(&self->Identifier), *matchingDefine);
                // result = tok_plus;
                printf("Define %s matched we should do something\n", defineName);
            }
        }
        if (IsMacro(self, result))
        {
            HandleMacro(self, result);
        }
        else if(result && result->TokenType == tok_hash)
        {
            result = NextToken();
            if (!result || result->TokenType != tok_identifier)
            {
LexpectedIdent:
                ParseError(self->LexerState, "Expected Identifier after #");
                return result;
            }
            if (result->TokenType == tok_identifier)
            {
                switch(result->IdentifierKey)
                {
                case define_key :
                    result = NextToken();

                    if(result->TokenType == tok_identifier)
                    {
                        metac_token_t* define_name = result;
                        uint32_t define_idx = self->CurrentTokenIndex;
                        result = NextToken();
                        int nParameters = 0;
                        const bool isMacro = (result->TokenType == tok_lParen);
                        if (isMacro)
                        {
                            PeekMatch(tok_lParen);
                            for (;;)
                            {
                                if (PeekMatch(tok_dotdotdot))
                                {
                                    nParameters |= (1 << 31);
                                    if (!PeekMatch(tok_rParen))
                                    {
                                        ParseError(self->LexerState, "')' expected after ...");
                                        return result;
                                    }
                                    break;
                                }
                                if (PeekMatch(tok_rParen))
                                    break;

                                if (!PeekMatch(tok_identifier))
                                    goto LexpectedIdent;
                                if (!PeekMatch(tok_comma))
                                {
                                    ParseErrorF(self->LexerState,
                                        "Expected ',' after define parameter %s",
                                        IDENTIFIER_PTR(MEMBER_SUFFIX(&self->Identifier),
                                                       *result));
                                    return result;
                                }
                                nParameters++;
                            }
                        }
                        AddDefine(self, define_name, nParameters);
                        result = NextToken();
                    }
                    break;
                default:
                    ParseErrorF(self->LexerState, "Expected define ifdef endif or got: %s",
                        IDENTIFIER_PTR(MEMBER_SUFFIX(&self->Identifier), *result));
                }
            }
        }
    }
    else
    {
        // TODO Error
    }

    return result;
}

static inline uint32_t MetaCParser_HowMuchLookahead(metac_parser_t* self)
{
    return (self->Lexer->TokenSize - self->CurrentTokenIndex);
}


metac_token_t* MetaCParser_PeekToken(metac_parser_t* self, int32_t p)
{
    metac_token_t* result = 0;
    assert(self->Lexer->TokenSize);

    if (cast(uint32_t)(self->CurrentTokenIndex + (p - 1)) < self->Lexer->TokenSize)
    {
        result = self->Lexer->Tokens + self->CurrentTokenIndex + (p - 1);
        if (IsMacro(self, result))
        {
            HandleMacro(self, result);
        }
        else if(result && result->TokenType == tok_hash)
        {
            int lookahead = MetaCParser_HowMuchLookahead(self);
            while (p < lookahead)
            {
                result = self->Lexer->Tokens + self->CurrentTokenIndex + (p++);
                if (result->TokenType == tok_newline)
                {

                }
            }
            HandlePreprocessor(self);
        }
    }
    else
    {
        // TODO Error
    }

    return result;
}

#define MetaCParser_Match(SELF, TYPE) \
    (MetaCParser_Match_((SELF), (TYPE), __FILE__, __LINE__))

metac_token_t* MetaCParser_Match_(metac_parser_t* self, metac_token_enum_t type,
                                 const char* filename, uint32_t lineNumber)
{
    metac_token_t* token = MetaCParser_NextToken(self);
    metac_token_enum_t got = (token ? token->TokenType : tok_eof);
    if (got != type)
    {
        if (got != tok_eof)
        {
            metac_location_t loc = self->Lexer->LocationStorage.Locations[token->LocationId - 4];

            printf("[%s:%u] Expected: %s -- Got: %s {line: %u: col: %u}\n",
                filename, lineNumber,
                MetaCTokenEnum_toChars(type), MetaCTokenEnum_toChars(got),
                loc.StartLine, loc.StartColumn);
        }
        else
        {
            printf("[%s:%u] Expected: %s -- Got: End of file\n",
                filename, lineNumber,
                MetaCTokenEnum_toChars(type));
        }
    }
    return token;
}

#ifndef ALIGN4
#  define ALIGN4(N) \
      (((N) + 3) & ~3)
#endif

metac_expression_kind_t BinExpTypeFromTokenType(metac_token_enum_t tokenType)
{
    metac_expression_kind_t result = exp_invalid;
    if (((tokenType >= FIRST_BINARY_TOKEN(TOK_SELF)) & (tokenType <= LAST_BINARY_TOKEN(TOK_SELF))))
    {
        result = cast(metac_expression_kind_t)(cast(int)tokenType -
                (cast(int)FIRST_BINARY_TOKEN(TOK_SELF) -
                 cast(int)FIRST_BINARY_EXP(TOK_SELF)));
    }

    if (tokenType == tok_lParen)
        result = exp_call;

    if (tokenType == tok_lBracket)
        result = exp_index;

    return result;
}

const char* BinExpTypeToChars(metac_binary_expression_kind_t t)
{
    switch(t)
    {
        case bin_exp_invalid: assert(0);

        case exp_comma     : return ",";
        case exp_dot       : return ".";
        case exp_dotdot    : return "..";
        case exp_arrow     : return "->";

        case exp_add       : return "+";
        case exp_sub       : return "-";
        case exp_mul       : return "*";
        case exp_div       : return "/";
        case exp_rem       : return "%";
        case exp_xor       : return "^";
        case exp_or        : return "|";
        case exp_and       : return "&";
        case exp_cat       : return "~";
        case exp_lsh       : return "<<";
        case exp_rsh       : return ">>";

        case exp_oror      : return "||";
        case exp_andand    : return "&&";

        case exp_assign    : return "=";

        case exp_add_ass   : return "+=";
        case exp_sub_ass   : return "-=";
        case exp_mul_ass   : return "*=";
        case exp_div_ass   : return "/=";
        case exp_rem_ass   : return "%=";
        case exp_xor_ass   : return "^=";
        case exp_or_ass    : return "|=";
        case exp_and_ass   : return "&=";
        case exp_cat_ass   : return "~=";
        case exp_lsh_ass   : return "<<=";
        case exp_rsh_ass   : return ">>=";

        case exp_eq        : return "==";
        case exp_neq       : return "!=";
        case exp_lt        : return "<";
        case exp_le        : return "<=";
        case exp_gt        : return ">";
        case exp_ge        : return ">=";
        case exp_spaceship : return "<=>";
    }

    assert(0);
    return 0;
}

metac_expression_kind_t ExpTypeFromTokenType(metac_token_enum_t tokenType)
{
    if (tokenType == tok_uint)
    {
        return exp_signed_integer;
    }
    else if (tokenType == tok_string)
    {
        return exp_string;
    }
    else if (tokenType == tok_lParen)
    {
        return exp_paren;
    }
    else if (tokenType == tok_kw_inject)
    {
        return exp_inject;
    }
    else if (tokenType == tok_kw_eject)
    {
        return exp_eject;
    }
    else if (tokenType == tok_kw_assert)
    {
        return exp_assert;
    }
    else if (tokenType == tok_dollar)
    {
        return exp_outer;
    }
    else if (tokenType == tok_and)
    {
        return exp_addr_or_and;
    }
    else if (tokenType == tok_star)
    {
        return exp_ptr_or_mul;
    }
    else if (tokenType == tok_identifier)
    {
        return exp_identifier;
    }
    else
    {
        assert(0);
        return exp_invalid;
    }

}

static inline void LexString(metac_lexer_t* lexer, const char* line)
{
    uint32_t line_length = strlen(line);
    metac_lexer_state_t lexer_state =
        MetaCLexerStateFromString(0, line);

    while(line_length > 0)
    {
        uint32_t initialPosition = lexer_state.Position;

        metac_token_t token =
            *MetaCLexerLexNextToken(lexer, &lexer_state, line, line_length);

        uint32_t eaten_chars = lexer_state.Position - initialPosition;
        line += eaten_chars;
        line_length -= eaten_chars;
    }
}

bool MetaCParser_PeekMatch(metac_parser_t* self, metac_token_enum_t expectedType, bool optional)
{
    metac_token_t* peekToken =
        MetaCParser_PeekToken(self, 1);
    bool result = true;

    if (!peekToken || peekToken->TokenType != expectedType)
    {
        result = false;
        if (!optional)
        {
            ParseErrorF(self->LexerState, "expected %s but got %s",
                MetaCTokenEnum_toChars(expectedType),
                MetaCTokenEnum_toChars(peekToken ? peekToken->TokenType : tok_eof)
            );
        }
    }

    return result;
}

static inline bool IsPostfixOperator(metac_token_enum_t t)
{
    return (t == tok_plusplus || t == tok_minusminus);
}

static inline bool IsBinaryOperator(metac_token_enum_t t)
{
    return ((t >= FIRST_BINARY_TOKEN(TOK_SELF) && t <= LAST_BINARY_TOKEN(TOK_SELF))
            || t == tok_lParen || t == tok_lBracket);
}

static inline uint32_t Mix(uint32_t a, uint32_t b)
{
  return (a ^ b) + 0x9e3779b9 + (a << 6) + (a >> 2);

}

typedef enum precedence_level_t
{
    prec_none,
    prec_comma,
    prec_op_assign,
    prec_oror,
    prec_andand,
    prec_or,
    prec_xor,
    prec_and,
    prec_eq,
    prec_cmp,
    prec_shift,
    prec_add,
    prec_mul,
    prec_max
} precedence_level_t;

static inline uint32_t OpToPrecedence(metac_expression_kind_t exp)
{
    if (exp == exp_comma)
    {
        return 1;
    }
    else if (exp >= exp_assign && exp <= exp_rsh_ass)
    {
        return 2;
    }
    //else if (exp_ternary)
    //{
    //  return 3;
    //}
    else if (exp == exp_oror)
    {
        return 4;
    }
    else if (exp == exp_andand)
    {
        return 5;
    }
    else if (exp == exp_or)
    {
        return 7;
    }
    else if (exp == exp_xor)
    {
        return 8;
    }
    else if (exp == exp_and)
    {
        return 9;
    }
    else if (exp == exp_eq || exp == exp_neq)
    {
        return 10;
    }
    else if (exp >= exp_lt && exp <= exp_ge)
    {
        return 11;
    }
    else if (exp == exp_rsh || exp == exp_lsh)
    {
        return 12;
    }
    else if (exp == exp_add || exp == exp_sub)
    {
        return 13;
    }
    else if (exp == exp_div || exp == exp_mul || exp == exp_rem)
    {
        return 14;
    }
    else if (exp == exp_ptr || exp == exp_arrow || exp == exp_dot || exp == exp_addr)
    {
        return 15;
    }
    else if (exp == exp_call || exp == exp_index
          || exp == exp_compl)
    {
        return 16;
    }
    else if (exp == exp_umin || exp == exp_unary_dot)
    {
        return 17;
    }
    else if (exp == exp_paren
          || exp == exp_signed_integer
          || exp == exp_string
          || exp == exp_identifier
          || exp == exp_char)
    {
        return 18;
    }
    assert(0);
    return 0;
}

static inline bool IsPrimaryExpressionToken(metac_token_enum_t tokenType)
{
    switch(tokenType)
    {
    case tok_lParen:
    case tok_uint:
    case tok_string:
    case tok_char:
    case tok_identifier:
        return true;
    default:
        return false;
    }
}

static inline bool IsTypeToken(metac_token_enum_t tokenType)
{
    bool  result =
           (   tokenType == tok_kw_const
            || (tokenType >= tok_kw_auto && tokenType <= tok_kw_double)
            || tokenType == tok_kw_unsigned
            || tokenType == tok_kw_signed
            || tokenType == tok_star
            || tokenType == tok_kw_struct
            || tokenType == tok_kw_enum
            || tokenType == tok_kw_union
            || tokenType == tok_identifier );
    return result;
}

static inline bool IsPunctuationToken(metac_token_enum_t tok)
{
    return (tok == tok_dot
        ||  tok == tok_dotdot
        ||  tok == tok_comma
        ||  tok == tok_semicolon
        ||  tok == tok_arrow
        ||  tok == tok_div
        ||  tok == tok_andand
        ||  tok == tok_oror
        ||  tok == tok_cat);
}

static bool CouldBeCast(metac_parser_t* self, metac_token_enum_t tok)
{
    bool result = true;

    if (tok != tok_lParen)
        return false;

    // first we see if the next could be a type token
    // because if it isn't then we are certainly not as cast
    metac_token_t* peek;
    bool seenStar = false;
    int rParenPos = 0;
    for(int peekCount = 2;
        (peek = MetaCParser_PeekToken(self, peekCount)), peek;
        peekCount++)
    {
        if (peek->TokenType == tok_rParen)
        {
            rParenPos = peekCount;
            break;
        }
        if (peek->TokenType == tok_star)
        {
            seenStar = true;
            if (peekCount == 2)
                return false;
        }
        if (!IsTypeToken(peek->TokenType))
        {
            return false;
        }
        else
        {
            if (peek->TokenType == tok_identifier && seenStar)
                return false;
        }
    }

    if (rParenPos)
    {
        metac_token_t* afterParen =
            MetaCParser_PeekToken(self, rParenPos + 1);
        if (!afterParen || IsPunctuationToken(afterParen->TokenType))
            return false;
    }

    return true;
}

decl_type_t* MetaCParser_ParseTypeDeclaration(metac_parser_t* self,
                                              metac_declaration_t* parent,
                                              metac_declaration_t* prev);

metac_expression_t* MetaCParser_ParsePrimaryExpression(metac_parser_t* self)
{
    metac_expression_t* result = 0;

    metac_token_t* currentToken = MetaCParser_PeekToken(self, 1);
    metac_token_enum_t tokenType =
        (currentToken ? currentToken->TokenType : tok_eof);

    if (tokenType == tok_lParen && CouldBeCast(self, tokenType))
    {
        // Not implemented right now
        result = AllocNewExpression(exp_cast);
        //typedef unsigned int b;
        MetaCParser_Match(self, tok_lParen);
        result->CastType = MetaCParser_ParseTypeDeclaration(self, 0, 0);
        MetaCParser_Match(self, tok_rParen);
        result->CastExp = MetaCParser_ParseExpression(self, expr_flags_none, 0);
    }
    else if (tokenType == tok_uint)
    {
        MetaCParser_Match(self, tok_uint);
        result = AllocNewExpression(exp_signed_integer);
        result->ValueI64 = currentToken->ValueU64;
        result->Hash = crc32c(~0, &result->ValueU64, sizeof(result->ValueU64));
        //PushOperand(result);
    }
    else if (tokenType == tok_string)
    {
        // result = GetOrAddStringLiteral(_string_table, currentToken);
        MetaCParser_Match(self, tok_string);
        result = AllocNewExpression(exp_string);
        result->StringPtr = RegisterString(self, currentToken);
        result->StringKey = currentToken->StringKey;
        result->Hash = currentToken->StringKey;
        //PushOperand(result);
    }
    else if (tokenType == tok_char)
    {
        MetaCParser_Match(self, tok_char);
        result = AllocNewExpression(exp_char);
        const uint32_t length = currentToken->charLength;
        const char* chars = currentToken->chars;
        const uint32_t hash = crc32c(~0, chars, length);

        (*(uint64_t*)result->Chars) =
            (*(uint64_t*) chars);
        result->CharKey = CHAR_KEY(hash, length);
    }
    else if (tokenType == tok_identifier)
    {
        result = AllocNewExpression(exp_identifier);
        MetaCParser_Match(self, tok_identifier);
        result->IdentifierPtr = RegisterIdentifier(self, currentToken);
        result->IdentifierKey = currentToken->IdentifierKey;
        result->Hash = currentToken->IdentifierKey;
        //PushOperand(result);
    }
    else if (tokenType == tok_lParen)
    {
        self->OpenParens++;
        MetaCParser_Match(self, tok_lParen);
        result = AllocNewExpression(exp_paren);
        {
            if (!MetaCParser_PeekMatch(self, tok_rParen, 1))
                result->E1 = MetaCParser_ParseExpression(self, expr_flags_none, 0);
        }
        //PushOperator(exp_paren);
        result->Hash = Mix(crc32c(~0, "()", 2), result->E1->Hash);
        //PushOperand(result);
        MetaCParser_Match(self, tok_rParen);
        self->OpenParens--;
        //PopOperator(exp_paren);
    }
    else
    {
        assert(0); // Not a primary Expression;
    }

    return result;
}

metac_expression_t* MetaCParser_ParsePostfixExpression(metac_parser_t* self,
                                                       metac_expression_t* left)
{
    metac_expression_t* result = 0;

    metac_token_t* peek = MetaCParser_PeekToken(self, 1);

    metac_token_enum_t peekTokenType = peek->TokenType;

    if (peekTokenType == tok_plusplus)
    {
        MetaCParser_Match(self, peekTokenType);
        metac_expression_t* E1 = left;
        result = AllocNewExpression(exp_post_increment);
        result->E1 = E1;
    }
    else if (peekTokenType == tok_minusminus)
    {
        MetaCParser_Match(self, peekTokenType);

        metac_expression_t* E1 = left;
        result = AllocNewExpression(exp_post_decrement);
        result->E1 = E1;
    }
    else
        assert(!"Unknown postfix expression, this function should never have been called");

    return result;
}
decl_type_t* MetaCParser_ParseTypeDeclaration(metac_parser_t* self, metac_declaration_t* parent, metac_declaration_t* prev);

static inline metac_expression_t* ParseDotCompilerExpression(metac_parser_t* self)
{
    metac_expression_t* result = 0;
    MetaCParser_Match(self, tok_dot);
    metac_token_t* peek;
    peek = MetaCParser_PeekToken(self, 1);
    if (!peek)
    {
        fprintf(stderr, "Expected . expression after '.compiler'\n");
    }

    return 0;
}

static inline uint32_t PtrVOnMatch(metac_parser_t* self,
                                   metac_identifier_ptr_t ptr,
                                   const char* matchStr,
                                   uint32_t matchLen)
{
    const char* idChars = IdentifierPtrToCharPtr(&self->IdentifierTable,
                                                 ptr);
    return ((!memcmp(idChars, matchStr, matchLen)) ? ptr.v : 0);
}


static inline metac_expression_t* ParseUnaryDotExpression(metac_parser_t* self)
{
#define compiler_key 0x8481e0
    metac_expression_t* result = 0;

    metac_token_t* peek;
    peek = MetaCParser_PeekToken(self, 1);
    if (peek && peek->TokenType == tok_identifier)
    {
        switch(peek->IdentifierKey)
        {
            case compiler_key:
            {
                metac_identifier_ptr_t identifierPtr =
                    RegisterIdentifier(self, peek);
                printf("Probably saw '.compiler'\n");
                if (UNLIKELY(self->SpecialNamePtr_Compiler.v == 0))
                {
                    const uint32_t compilerPtrV =
                        PtrVOnMatch(self, identifierPtr, "compiler", 8);
                    if(compilerPtrV)
                        self->SpecialNamePtr_Compiler.v = compilerPtrV;
                }
#if 0

                printf("SpecialNamePtr_Compiler: %u\n",
                    self->SpecialNamePtr_Compiler.v);
                printf("identifierPtr.v: %u\n",
                    identifierPtr.v);
#endif
                if (self->SpecialNamePtr_Compiler.v == identifierPtr.v)
                {
                    MetaCParser_Match(self, tok_identifier);
                    result = ParseDotCompilerExpression(self);
                }
            } break;
        }
    }

    if (!result)
    {
        result = AllocNewExpression(exp_unary_dot);
        result->E1 = MetaCParser_ParseExpression(self, expr_flags_unary, 0);
        result->Hash = Mix(
            crc32c(~0, ".", sizeof(".") - 1),
            result->E1->Hash
        );
    }

    return result;
}

metac_expression_t* MetaCParser_ParseUnaryExpression(metac_parser_t* self)
{
    metac_expression_t* result = 0;

    metac_token_t* currentToken = MetaCParser_PeekToken(self, 1);
    metac_token_enum_t tokenType =
        (currentToken ? currentToken->TokenType : tok_eof);


    if (tokenType == tok_dot)
    {
        MetaCParser_Match(self, tok_dot);
        result = ParseUnaryDotExpression(self);
    }
    else if (tokenType == tok_kw_eject)
    {
        MetaCParser_Match(self, tok_kw_eject);
        result = AllocNewExpression(exp_eject);
        //PushOperator(exp_eject);
        result->E1 = MetaCParser_ParseExpression(self, expr_flags_none, 0);
        result->Hash = Mix(
            crc32c(~0, "eject", sizeof("eject") - 1),
            result->E1->Hash
        );
        //PushOperand(result);
        //PopOperator(exp_eject);
    }
    else if (tokenType == tok_kw_inject)
    {
        MetaCParser_Match(self, tok_kw_inject);
        result = AllocNewExpression(exp_inject);
        //PushOperator(exp_inject);
        result->E1 = MetaCParser_ParseExpression(self, expr_flags_none, 0);
        result->Hash = Mix(
            crc32c(~0, "inject", sizeof("inject") - 1),
            result->E1->Hash
        );
        //PushOperand(result);
        //PopOperator(exp_inject);
    }
    else if (tokenType == tok_kw_typeof)
    {
        MetaCParser_Match(self, tok_kw_typeof);
        result = AllocNewExpression(exp_typeof);
        //PushOperator(exp_typeof);
        metac_token_t* nextToken = MetaCParser_PeekToken(self, 1);
        if (!nextToken || nextToken->TokenType != tok_lParen)
        {
            ParseError(self->LexerState, "Expected typeof to be followed by '('");
        }

        metac_expression_t* parenExp = MetaCParser_ParseExpression(self, expr_flags_none, 0);
        //PopOperator(exp_typeof);
        assert(parenExp->Kind == exp_paren);
        result->E1 = parenExp->E1;
    }
    else if (tokenType == tok_kw_sizeof)
    {
        MetaCParser_Match(self, tok_kw_sizeof);
        result = AllocNewExpression(exp_sizeof);
        metac_token_t* nextToken = MetaCParser_PeekToken(self, 1);
        bool wasParen = false;
        if (nextToken->TokenType == tok_lParen)
        {
            wasParen = true;
            MetaCParser_Match(self, tok_lParen);
        }
        result->SizeofType = MetaCParser_ParseTypeDeclaration(self, 0, 0);
        if (wasParen)
        {
            MetaCParser_Match(self, tok_rParen);
        }
    }
    else if (tokenType == tok_kw_assert)
    {
        MetaCParser_Match(self, tok_kw_assert);
        result = AllocNewExpression(exp_assert);
        //PushOperator(exp_assert);
        metac_token_t* nextToken = MetaCParser_PeekToken(self, 1);
        if (!nextToken || nextToken->TokenType != tok_lParen)
        {
            ParseError(self->LexerState, "Expected assert to be followed by '('");
        }
        metac_expression_t* parenExp = MetaCParser_ParseExpression(self, expr_flags_none, 0);
        //PopOperator(exp_assert);
        assert(parenExp->Kind == exp_paren);
        result->E1 = parenExp->E1;
    }
    else if (tokenType == tok_minus)
    {
        MetaCParser_Match(self, tok_minus);
        result = AllocNewExpression(exp_umin);
        //PushOperator(exp_addr);
        result->E1 = MetaCParser_ParseExpression(self, expr_flags_unary, result);
        result->Hash = Mix(
            crc32c(~0, "-", 1),
            result->E1->Hash
        );
    }
    else if (tokenType == tok_and)
    {
        MetaCParser_Match(self, tok_and);
        result = AllocNewExpression(exp_addr);
        //PushOperator(exp_addr);
        result->E1 = MetaCParser_ParseExpression(self, expr_flags_unary, result);
        result->Hash = Mix(
            crc32c(~0, "&", sizeof("&") - 1),
            result->E1->Hash
        );
        //PushOperand(result);
        //PopOperator(exp_addr);
    }
    else if (tokenType == tok_star)
    {
        MetaCParser_Match(self, tok_star);
        result = AllocNewExpression(exp_ptr);
        //PushOperator(exp_ptr);
        result->E1 = MetaCParser_ParseExpression(self, expr_flags_unary, 0);
        result->Hash = Mix(
            crc32c(~0, "*", sizeof("*") - 1),
            result->E1->Hash
        );
        //PushOperand(result);
    }
    else if (tokenType == tok_bang)
    {
        MetaCParser_Match(self, tok_bang);
        result = AllocNewExpression(exp_not);
        result->E1 = MetaCParser_ParseExpression(self, expr_flags_unary, 0);
        result->Hash = Mix(
            crc32c(~0, "!", 1),
            result->E1->Hash
        );
    }
    else if (tokenType == tok_cat)
    {
        MetaCParser_Match(self, tok_cat);
        result = AllocNewExpression(exp_compl);
        result->E1 = MetaCParser_ParseExpression(self, expr_flags_unary, 0);
        result->Hash = Mix(
            crc32c(~0, "~", 1),
            result->E1->Hash
        );
    }
    else if (IsPrimaryExpressionToken(tokenType))
    {
        result = MetaCParser_ParsePrimaryExpression(self);
    }
    else
    {
        if (tokenType != tok_eof)
        {
            metac_location_t location =
                self->Lexer->LocationStorage.Locations[currentToken->LocationId - 4];
            fprintf(stderr, "line: %d col: %d\n", location.StartLine, location.StartColumn);
        }
        fprintf(stderr, "Unexpected Token: %s\n", MetaCTokenEnum_toChars(tokenType));
 //       assert(0);
    }

    metac_token_t* peek_post = MetaCParser_PeekToken(self, 1);
    metac_token_enum_t postTokenType =
        (peek_post ? peek_post->TokenType : tok_invalid);

    if (IsPostfixOperator(postTokenType))
    {
        result = MetaCParser_ParsePostfixExpression(self, result);
    }

    return result;
}
typedef struct binexp_stack_slot_t {
  metac_expression_t* Exp;

} binexp_stack_slot_t;

static inline void XChangeExpressionStack(binexp_stack_slot_t* stack, uint32_t sp)
{
    assert(sp > 1);
    binexp_stack_slot_t* src_slot    = &stack[--sp];
    binexp_stack_slot_t* target_slot = &stack[sp - 1];

    metac_expression_t* newLhs = target_slot->Exp;

    target_slot->Exp = AllocNewExpression(src_slot->Exp->Kind);
    target_slot->Exp->E1 = newLhs->E1;
    target_slot->Exp->E2 = src_slot->Exp;
}

#define HAS_GRPAHVIZ
#ifdef HAS_GRAPHVIZ
#include <graphviz/gvc.h>
#include <graphviz/cgraph.h>
#endif

static void PushOpStack(metac_parser_t* self, metac_expression_kind_t expKind)
{
#ifndef NO_DOT_PRINTER
    metac_dot_printer_label_t* CurrentLabel =
        self->DotPrinter->CurrentLabel;
    const char* op =
        BinExpTypeToChars((metac_binary_expression_kind_t)expKind);
    if (!CurrentLabel)
    {
        self->DotPrinter->CurrentLabel = MetaCDotPrinter_BeginLabel(self->DotPrinter);
    }
    {
        Dot_PrintString(self->DotPrinter, op);
    }
#endif
}

static void PopOpStack()
{

}

metac_expression_t* MetaCParser_ParseBinaryExpression(metac_parser_t* self,
                                                      metac_expression_t* left,
                                                      uint32_t min_prec)
{
    metac_expression_t* result = 0;

    uint32_t sp = 0;
    binexp_stack_slot_t binexp_stack[prec_max];
//    metac_location_t* startLocation =
//        self->Lexer->LocationStorage.Locations + (left->LocationIdx - 4);

    metac_token_t* peekToken;
    metac_token_enum_t peekTokenType;

    peekToken = MetaCParser_PeekToken(self, 1);
    peekTokenType = (peekToken ? peekToken->TokenType : tok_eof);
    // a * b + c * d
    // a * b | (a * b) + c |
    // here we pop the stack

    if (peekTokenType == tok_lBracket)
    {
        MetaCParser_Match(self, tok_lBracket);

        metac_expression_t* E1 = left;
        result = AllocNewExpression(exp_index);
        result->E1 = E1;
        result->E2 = MetaCParser_ParseExpression(self, expr_flags_none, 0);

        MetaCParser_Match(self, tok_rBracket);
    }
    else if (peekTokenType == tok_lParen)
    {
        self->OpenParens++;
        MetaCParser_Match(self, tok_lParen);
        metac_expression_t* E1 = left;
        assert(peekToken);

        metac_token_t* peekToken = MetaCParser_PeekToken(self, 1);
        exp_argument_t* arguments = (exp_argument_t*) _emptyPointer;
        exp_argument_t** nextArgument = &arguments;

        if (!MetaCParser_PeekMatch(self, tok_rParen, true))
        {
            for (;;)
            {
                assert((*nextArgument) == _emptyPointer);

                (*nextArgument) = (exp_argument_t*)AllocNewExpression(exp_argument);
                ((*nextArgument)->Expression) = MetaCParser_ParseExpression(self, expr_flags_call, 0);
                nextArgument = &((*nextArgument)->Next);
                (*nextArgument) = (exp_argument_t*) _emptyPointer;
                if(MetaCParser_PeekMatch(self, tok_comma, true))
                {
                    MetaCParser_Match(self, tok_comma);
                }
                else
                {
                    break;
                }
            }
            MetaCParser_Match(self, tok_rParen);
            self->OpenParens--;
        }
        result = AllocNewExpression(exp_call);
        result->E1 = E1;
        result->E2 = (metac_expression_t*)arguments;

        //PopOperator(exp_call);
    }
    else if (IsBinaryOperator(peekTokenType))
    {
#if 0
        while (IsBinaryOperator(peekTokenType))
        {
            MetaCParser_Match(self, peekTokenType);
            metac_expression_kind_t expKind = BinExpTypeFromTokenType(peekTokenType);
            uint32_t OpPrec = OpToPrecedence(expKind);
            if (OpPrec >
                (sp ? OpToPrecedence(binexp_stack[sp - 1].Exp->Kind) : 0))
            {
                PushOpStack(self, expKind);
                binexp_stack_slot_t* slot = &binexp_stack[sp++];
                slot->Exp = AllocNewExpression(expKind);
                slot->Exp->E1 = left;
                slot->Exp->E2 = MetaCParser_ParseUnaryExpression(self);
                left = slot->Exp;
                // continue;
            }
            else // this is when we have a lower or equal precence operator
            {
                assert(sp >= 1);
                while(sp && (OpPrec <= OpToPrecedence(binexp_stack[sp - 1].Exp->Kind)))
                {
                    binexp_stack_slot_t* slot = &binexp_stack[--sp];
                    PopOpStack();
                    result = AllocNewExpression(expKind);
                    result->E1 = slot->Exp;
                    result->E2 = MetaCParser_ParseUnaryExpression(self);
                }
            }
            peekToken = MetaCParser_PeekToken(self, 1);
            peekTokenType = (peekToken ? peekToken->TokenType : tok_eof);
        }
        while (sp > 1)
        {
            XChangeExpressionStack(binexp_stack, sp--);
        }
        if (sp == 1)
            result = binexp_stack[0].Exp;
#else
        metac_expression_kind_t exp_left;
        metac_expression_kind_t exp_right;

        while(IsBinaryOperator(peekTokenType)
           && OpToPrecedence(BinExpTypeFromTokenType(peekTokenType)) >= min_prec)
        {
            exp_right = BinExpTypeFromTokenType(peekTokenType);
            uint32_t opPrecedence = OpToPrecedence(exp_right);
            MetaCParser_Match(self, peekTokenType);
            metac_expression_t* rhs = MetaCParser_ParseUnaryExpression(self);
            peekToken = MetaCParser_PeekToken(self, 1);
            peekTokenType = (peekToken ? peekToken->TokenType : tok_eof);
            while(IsBinaryOperator(peekTokenType)
               && opPrecedence <
                  OpToPrecedence(BinExpTypeFromTokenType(peekTokenType)))
            {
                rhs = MetaCParser_ParseBinaryExpression(self, rhs, opPrecedence + 0);
                peekToken = MetaCParser_PeekToken(self, 1);
                peekTokenType = (peekToken ? peekToken->TokenType : tok_eof);
            }
            result = AllocNewExpression(exp_right);
            result->E1 = left;
            result->E2 = rhs;
            left = result;
        }
#endif
    }
    else
    {
        assert(!"Unexpected Token");
    }

    return result;
}

bool IsBinaryExp(metac_expression_kind_t kind)
{
    return ((kind >= FIRST_BINARY_EXP(TOK_SELF)) && (kind <= LAST_BINARY_EXP(TOK_SELF)));
}

bool IsBinaryAssignExp(metac_expression_kind_t kind)
{
   return (kind >= exp_add_ass && kind  <= exp_rsh_ass);
}


metac_expression_t* MetaCParser_ParseExpression(metac_parser_t* self,
                                                parse_expression_flags_t eflags,
                                                metac_expression_t* prev)
{
    metac_expression_t* result = 0;
    metac_token_t* currentToken = MetaCParser_PeekToken(self, 1);
    metac_token_enum_t tokenType =
        (currentToken ? currentToken->TokenType : tok_invalid);

    if (IsPrimaryExpressionToken(tokenType))
    {
        result = MetaCParser_ParsePrimaryExpression(self);
    }
    else  if (!prev /*|| prec_left > prec_right*/)
    {
        result = MetaCParser_ParseUnaryExpression(self);
    }
    else
    {
        result = MetaCParser_ParseBinaryExpression(self, prev, 0);
    }

//    printf("TokenType: %s\n", MetaCTokenEnum_toChars(tokenType));

    metac_token_t* peekNext = MetaCParser_PeekToken(self, 1);
    if (peekNext)
    {
        uint32_t min_prec = 0;

        tokenType = peekNext->TokenType;
        //within a call we must not treat a comma as a binary expression
        if ((eflags & expr_flags_call) && tokenType == tok_comma)
            goto LreturnExp;

        if ((eflags & expr_flags_unary))
            goto LreturnExp;

        if (IsBinaryOperator(tokenType))
        {
            //uint32_t prec = OpToPrecedence(BinExpTypeFromTokenType(tokenType));
            uint32_t prec = OpToPrecedence(result->Kind);
            if (prec < min_prec)
                return result;
            result = MetaCParser_ParseBinaryExpression(self, result, min_prec);
        }
        else if (IsPostfixOperator(tokenType))
        {
            result = MetaCParser_ParsePostfixExpression(self, result);
        }
        else if (peekNext->TokenType == tok_lParen)
        {
            result = MetaCParser_ParseBinaryExpression(self, result, OpToPrecedence(exp_call));
        }
        else if (tokenType == tok_lBracket)
        {
            result = MetaCParser_ParseBinaryExpression(self, result, OpToPrecedence(exp_index));
        }
        else if (tokenType == tok_rBracket || tokenType == tok_rParen)
        {
            // there's nothing to see here crray on
        }
        //else assert(!"Stray Input");
    }

LreturnExp:
    return result;
}

static inline bool IsDeclType(metac_declaration_t* decl)
{
    metac_declaration_kind_t kind = decl->DeclKind;
    return (kind == decl_type
         || kind == decl_type_struct
         || kind == decl_type_enum
         || kind == decl_type_union);
}


#define ErrorDeclaration() \
    (metac_declaration_t*)0

#define ErrorTypeDeclaration() \
    (decl_type_t*)0

#define U32(VAR) \
	(*(uint32_t*)(&VAR))
static decl_type_array_t* ParseArraySuffix(metac_parser_t* self, decl_type_t* type);

decl_type_t* MetaCParser_ParseTypeDeclaration(metac_parser_t* self, metac_declaration_t* parent, metac_declaration_t* prev)
{
    decl_type_t* result = 0;

    decl_type_t* type = AllocNewDeclaration(decl_type, &result);
    metac_type_modifiers typeModifiers = typemod_none;
    metac_token_t* currentToken = 0;

LnextToken:
    currentToken = MetaCParser_NextToken(self);
    metac_token_enum_t tokenType =
        (currentToken ? currentToken->TokenType : tok_invalid);

    bool postType = false;


    while(IsTypeToken(tokenType))
    {
        if (tokenType == tok_identifier)
        {
            type->TypeKind = type_identifier;
            type->TypeIdentifier = RegisterIdentifier(self, currentToken);
            U32(type->TypeModifiers) |= typeModifiers;
            break;
        }
        if (tokenType == tok_kw_const)
        {
            U32(typeModifiers) |= typemod_const;
            goto LnextToken;
        }
        else if (tokenType >= tok_kw_auto && tokenType <= tok_kw_double)
        {
            type->TypeKind = (metac_type_kind_t)(type_auto + (tokenType - tok_kw_auto));
            U32(type->TypeModifiers) |= typeModifiers;
            if (tokenType == tok_kw_long)
            {
                if (MetaCParser_PeekMatch(self, tok_kw_long, 1))
                {
                    MetaCParser_Match(self, tok_kw_long);
                    type->TypeKind = type_long_long;
                }
                else if (MetaCParser_PeekMatch(self, tok_kw_double, 1))
                {
                    MetaCParser_Match(self, tok_kw_double);
                    type->TypeKind = type_long_double;
                }
            }
            break;
        }
        else if (tokenType == tok_kw_unsigned)
        {
            U32(typeModifiers) |= typemod_unsigned;
            goto LnextToken;
        }
        else if (tokenType == tok_kw_signed)
        {
            //TODO mark signed
            goto LnextToken;
        }
        else if (tokenType == tok_star)
        {
            assert(0); // this is not supposed to happen
        }
        else if (tokenType == tok_kw_struct || tokenType == tok_kw_union)
        {
            bool isStruct = tokenType == tok_kw_struct;

            bool isPredeclated = true;

            decl_type_struct_t* struct_ = AllocNewDeclaration(decl_type_struct, &result);
            type = (decl_type_t*)struct_;
            if (!isStruct)
            {
                struct_->TypeKind = type_union;
                struct_->DeclKind = decl_type_union;
            }

            if (MetaCParser_PeekMatch(self, tok_identifier, 1))
            {
                metac_token_t* structName = MetaCParser_NextToken(self);
                struct_->Identifier = RegisterIdentifier(self, structName);
            }
            else
            {
                struct_->Identifier = empty_identifier;
            }

            if (tokenType == tok_kw_struct)
            {
                if (MetaCParser_PeekMatch(self, tok_colon, 1))
                {
                    MetaCParser_Match(self, tok_colon);
                    metac_token_t* baseName = MetaCParser_NextToken(self);
                    struct_->BaseIdentifier = RegisterIdentifier(self, baseName);
                }
                goto LSetEmptyBase;
            }
            else
            {
        LSetEmptyBase:
                struct_->BaseIdentifier = empty_identifier;
            }
            if (MetaCParser_PeekMatch(self, tok_lBrace, 1))
            {
                MetaCParser_Match(self, tok_lBrace);
                decl_field_t **nextMemberPtr = &struct_->Fields;

                isPredeclated = false;
                while(!MetaCParser_PeekMatch(self, tok_rBrace, 1))
                {
                    decl_field_t* field =
                        AllocNewDeclaration(decl_field, (metac_declaration_t**)
                                            nextMemberPtr);
                    field->Next = 0;
                    metac_declaration_t *decl =
                        MetaCParser_ParseDeclaration(self, result);
                    assert(decl->DeclKind == decl_variable);
                    field->Field = (decl_variable_t*)decl;

                    // MetaCParser_Match(self, tok_semicolon);
                    nextMemberPtr = &field->Next;
                    struct_->FieldCount++;
                }
                MetaCParser_Match(self, tok_rBrace);
            }
            else
            {
                printf("We just have a decl\n");
            }
            break;
        }
        else if (tokenType == tok_kw_enum)
        {
        }
    }

    currentToken = MetaCParser_PeekToken(self, 1);
    tokenType =
        (currentToken ? currentToken->TokenType : tok_invalid);

    while(tokenType == tok_star)
    {
        MetaCParser_Match(self, tok_star);
        decl_type_t* elementType = result;
        decl_type_ptr_t* ptr = AllocNewDeclaration(decl_type_ptr, &result);
        ptr->ElementType = elementType;

        currentToken = MetaCParser_PeekToken(self, 1);
        tokenType =
            (currentToken ? currentToken->TokenType : tok_invalid);
    }

    return result;
}

decl_parameter_list_t ParseParamterList(metac_parser_t* self)
{
    decl_parameter_list_t result = {0, emptyPointer};
    uint32_t parameterCount = 0;
    decl_parameter_t* dummy;
    decl_parameter_t** nextParam = &result.List;

    while (!MetaCParser_PeekMatch(self, tok_rParen, true))
    {
        assert((*nextParam) == emptyPointer);

        decl_parameter_t* param;
        AllocNewDeclaration(decl_parameter, &param);
        parameterCount++;
        (*nextParam) = param;

        param->Type = MetaCParser_ParseTypeDeclaration(self, &dummy, 0);

        param->Identifier = empty_identifier;
        if (MetaCParser_PeekMatch(self, tok_identifier, 1))
        {
            metac_token_t* nameToken = MetaCParser_Match(self, tok_identifier);
            param->Identifier = RegisterIdentifier(self, nameToken);

            // follow parameter
            while(MetaCParser_PeekMatch(self, tok_lBracket, true))
            {
                param->Type = (decl_type_t*)ParseArraySuffix(self, param->Type);
            }
        }

        nextParam = &param->Next;
        (*nextParam) = (decl_parameter_t*) _emptyPointer;

        if (MetaCParser_PeekMatch(self, tok_comma, true))
        {
            MetaCParser_Match(self, tok_comma);
        }
        else
        {
            assert(MetaCParser_PeekMatch(self, tok_rParen, true));
        }
    }
    MetaCParser_Match(self, tok_rParen);
    result.ParameterCount = parameterCount;

    return result;
}

static stmt_block_t* MetaCParser_ParseBlockStatement(metac_parser_t* self,
                                                     metac_statement_t* parent,
                                                     metac_statement_t* prev);

decl_function_t* ParseFunctionDeclaration(metac_parser_t* self, decl_type_t* type)
{
    decl_function_t result;

    metac_token_t* id = MetaCParser_Match(self, tok_identifier);
    metac_identifier_ptr_t identifier = RegisterIdentifier(self, id);

    MetaCParser_Match(self, tok_lParen);
    decl_function_t* funcDecl = AllocNewDeclaration(decl_function, &result);
    funcDecl->ReturnType = type;
    funcDecl->Identifier = identifier;
    funcDecl->FunctionBody = (stmt_block_t*) _emptyPointer;
    decl_parameter_list_t parameterList = ParseParamterList(self);
    funcDecl->Parameters = parameterList.List;
    funcDecl->ParameterCount = parameterList.ParameterCount;

    if (MetaCParser_PeekMatch(self, tok_lBrace, true))
    {
        funcDecl->FunctionBody = MetaCParser_ParseBlockStatement(self, 0, 0);
    }

    return funcDecl;
}

metac_declaration_t* MetaCParser_ParseDeclaration(metac_parser_t* self, metac_declaration_t* parent)
{
    metac_token_t* currentToken = MetaCParser_PeekToken(self, 1);
    metac_token_enum_t tokenType =
        (currentToken ? currentToken->TokenType : tok_invalid);

	metac_declaration_t* result = 0;
    bool isStatic = false;

    decl_type_t* type = 0;

    if (MetaCParser_PeekMatch(self, tok_kw_static, true))
    {
        isStatic = true;
        MetaCParser_Match(self, tok_kw_static);
        currentToken = MetaCParser_PeekToken(self, 1);
        tokenType = currentToken ? currentToken->TokenType : tok_eof;
    }

    if (IsTypeToken(tokenType))
    {
         type = MetaCParser_ParseTypeDeclaration(self, parent, 0);
         if (((tokenType == tok_kw_struct) | (tokenType == tok_kw_union)))
         {
             result = (metac_declaration_t*)type;
         }
    }

    if (tokenType == tok_kw_typedef)
    {
        MetaCParser_Match(self, tok_kw_typedef);
        currentToken = MetaCParser_PeekToken(self, 1);
            tokenType =
        (currentToken ? currentToken->TokenType : tok_invalid);
        decl_typedef_t* typdef = AllocNewDeclaration(decl_typedef, &result);

        typdef->Type = MetaCParser_ParseTypeDeclaration(self, (metac_declaration_t*) typdef, 0);
        metac_token_t* name = MetaCParser_Match(self, tok_identifier);
        if (!name || name->TokenType != tok_identifier)
        {
            printf("Expecting an identifier to follow the type definition of a typedef\n");
            return ErrorDeclaration();
        }
        typdef->Identifier = RegisterIdentifier(self, name);
        goto LendDecl;
    }

    if (type)
    {
        if (MetaCParser_PeekMatch(self, tok_lParen, true))
        {
            // this might be a function pointer
            MetaCParser_Match(self, tok_lParen);
            decl_variable_t* fPtrVar;
            self->OpenParens++;
            if (MetaCParser_PeekMatch(self, tok_star, true))
            {
                MetaCParser_Match(self, tok_star);
                // this is quite likely a function pointer
                metac_token_t* fPtrid = MetaCParser_PeekToken(self, 1);
                if (fPtrid->TokenType == tok_identifier)
                {
                    MetaCParser_Match(self, tok_identifier);
                    // this would be the function pointer name then
                    fPtrVar = AllocNewDeclaration(decl_variable, &result);
                    fPtrVar->VarInitExpression = _emptyPointer;

                    fPtrVar->VarIdentifier = RegisterIdentifier(self, fPtrid);
                    MetaCParser_Match(self, tok_rParen);
                    self->OpenParens--;
                    // we eat the paren before we do the parameterList
                    //TODO maybe paramter list parsing should eat both parens
                    MetaCParser_Match(self, tok_lParen);
                    decl_type_t* returnType = type;
                    decl_parameter_list_t paramterList =
                        ParseParamterList(self);

                    decl_type_functiontype_t* functionType =
                        AllocNewDeclaration(decl_type_functiontype, &fPtrVar->VarType);

                    functionType->ReturnType = returnType;
                    functionType->Parameters = paramterList.List;
                    functionType->ParameterCount = paramterList.ParameterCount;

                    fPtrVar->VarType = functionType;
                }
            }
        }
        else if (MetaCParser_PeekMatch(self, tok_identifier, 1))
        {
            metac_token_t* afterId = MetaCParser_PeekToken(self, 2);
            if (afterId->TokenType == tok_lParen)
            {
                decl_function_t* funcDecl = ParseFunctionDeclaration(self, type);
                result = (metac_declaration_t*) funcDecl;
            }
            else
            {
                decl_variable_t* varDecl = AllocNewDeclaration(decl_variable, &result);
                metac_token_t* idToken = MetaCParser_Match(self, tok_identifier);
                metac_identifier_ptr_t identifier = RegisterIdentifier(self, idToken);
//            varDecl.LocationIdx =
//                MetaCLocationStorage_StartLoc(&parser.locationStorage,
//                    MetaCLocationStorage_StartLine(&parser.lexer.locationStorage, type.LocationIdx));

                varDecl->VarType = type;
                varDecl->VarIdentifier = identifier;
                varDecl->VarInitExpression = (metac_expression_t*)_emptyPointer;

                while (MetaCParser_PeekMatch(self, tok_lBracket, true))
                {
                    varDecl->VarType = (decl_type_t*)ParseArraySuffix(self, varDecl->VarType);
                }
                if (MetaCParser_PeekMatch(self, tok_assign, true))
                {
                    MetaCParser_Match(self, tok_assign);
                    varDecl->VarInitExpression = MetaCParser_ParseExpression(self, expr_flags_none, 0);
                }
            }
        }
    }
    else
    {
        ParseError(self->LexerState, "A declaration is expected to start with  a type");
    }

LendDecl:
    // eat a semicolon if there is one this is more a repl kindof thing
    if (MetaCParser_PeekMatch(self, tok_semicolon, true))
        MetaCParser_Match(self, tok_semicolon);

	return result;
}

static decl_type_array_t* ParseArraySuffix(metac_parser_t* self, decl_type_t* type)
{
    decl_type_array_t* arrayType = 0;
    if (MetaCParser_PeekMatch(self, tok_lBracket, true))
    {
        MetaCParser_Match(self, tok_lBracket);
        decl_type_t* paramType = type;
        arrayType =
            AllocNewDeclaration(decl_type_array, &arrayType);
        //TODO ErrorMessage array must have numeric dimension
        arrayType->ElementType = type;
        arrayType->Dim = MetaCParser_ParseExpression(self, expr_flags_none, 0);
        MetaCParser_Match(self, tok_rBracket);
    }
    assert(arrayType);
    return arrayType;
}


#define ErrorStatement() \
    (metac_statement_t*)0

static metac_statement_t* MetaCParser_ParseStatement(metac_parser_t* self,
                                                     metac_statement_t* parent,
                                                     metac_statement_t* prev)
{
    metac_statement_t* result = 0;

    metac_token_t* currentToken = MetaCParser_PeekToken(self, 1);
    metac_token_enum_t tokenType =
        (currentToken ? currentToken->TokenType : tok_invalid);

    metac_token_t* peek2;

    if (tokenType == tok_invalid)
    {
        return ErrorStatement();
    }

    if (self->CurrentBlockStatement)
        self->CurrentBlockStatement->StatementCount++;

    if (tokenType == tok_kw_if)
    {
        stmt_if_t* if_stmt = AllocNewStatement(stmt_if, &result);
        MetaCParser_Match(self, tok_kw_if);
        if (!MetaCParser_PeekMatch(self, tok_lParen, 0))
        {
            ParseError(self->LexerState, "execpected ( after if\n");
            return ErrorStatement();
        }
        MetaCParser_Match(self, tok_lParen);
        metac_expression_t* condExpP =
            MetaCParser_ParseExpression(self, expr_flags_none, 0);
        MetaCParser_Match(self, tok_rParen);
        if_stmt->IfCond = condExpP;
        if_stmt->IfBody = MetaCParser_ParseStatement(self, (metac_statement_t*)result, 0);
        if (MetaCParser_PeekMatch(self, tok_kw_else, 1))
        {
            MetaCParser_Match(self, tok_kw_else);
            if_stmt->ElseBody = (metac_statement_t*)MetaCParser_ParseStatement(self, (metac_statement_t*)result, 0);
        }
        else
        {
            if_stmt->ElseBody = (metac_statement_t*)_emptyPointer;
        }
        goto LdoneWithStatement;
    }
    else if (tokenType == tok_kw_for)
    {
        stmt_for_t* for_ = AllocNewStatement(stmt_for, &result);
        uint32_t hash = crc32c(~0, "for", sizeof("for") - 1);

        result->Hash = hash;
    }
    else if (tokenType == tok_kw_switch)
    {
        uint32_t switchHash =
            crc32c(~0, "switch", sizeof("switch") - 1);
        stmt_switch_t* switch_ = AllocNewStatement(stmt_switch, &result);

        MetaCParser_Match(self, tok_kw_switch);
        MetaCParser_Match(self, tok_lParen);
        metac_expression_t* cond =
            MetaCParser_ParseExpression(self, expr_flags_none, 0);
        switchHash = Mix(switchHash, cond->Hash);
        MetaCParser_Match(self, tok_rParen);
        if (!MetaCParser_PeekMatch(self, tok_lBrace, 0))
        {
            ParseError(self->LexerState, "parsing switch failed\n");
            return ErrorStatement();
        }

        metac_statement_t* caseBlock =
            (metac_statement_t*)MetaCParser_ParseBlockStatement(self, result, 0);
        switchHash = Mix(switchHash, caseBlock->Hash);
    }
    else if (tokenType == tok_identifier
        && (peek2 = MetaCParser_PeekToken(self, 2))
        && (peek2->TokenType == tok_colon))
    {
            metac_token_t* label_tok = MetaCParser_Match(self, tok_identifier);
            MetaCParser_Match(self, tok_colon);

            stmt_label_t* label = AllocNewStatement(stmt_label, &result);

            label->Label = RegisterIdentifier(self, label_tok);
    }
    else if (tokenType == tok_kw_goto)
    {
        stmt_goto_t* goto_ = AllocNewStatement(stmt_goto, &result);
        uint32_t gotoHash = crc32c(~0, "goto", sizeof("goto") - 1);

        MetaCParser_Match(self, tok_kw_goto);
        metac_token_t* label = MetaCParser_Match(self, tok_identifier);
        goto_->Label = RegisterIdentifier(self, label);
        goto_->Hash = Mix(gotoHash, label->IdentifierKey);
    }
    else if (tokenType == tok_kw_break)
    {
        stmt_break_t* result = AllocNewStatement(stmt_break, &result);
        uint32_t hash = crc32c(~0, "break", sizeof("break") - 1);

        result->Hash = hash;
    }
    else if (tokenType == tok_kw_continue)
    {
        stmt_continue_t* result = AllocNewStatement(stmt_continue, &result);
        uint32_t hash = crc32c(~0, "break", sizeof("break") - 1);

        result->Hash = hash;
    }
    else if (tokenType == tok_kw_case)
    {
        uint32_t caseHash =
            crc32c(~0, "case", sizeof("case") - 1);
        stmt_case_t* case_ = AllocNewStatement(stmt_case, &result);

        MetaCParser_Match(self, tok_kw_case);
        metac_expression_t* caseExp =
            MetaCParser_ParseExpression(self, expr_flags_none, 0);
        MetaCParser_Match(self, tok_colon);

        caseHash = Mix(caseHash, caseExp->Hash);
        result->Next =
            MetaCParser_ParseStatement(self, result, 0);
        if (result->Next != _emptyPointer)
        {
            caseHash = Mix(caseHash, result->Next->Hash);
        }
        result->Hash = caseHash;
    }
    else if (tokenType == tok_kw_return)
    {
        stmt_return_t* return_ = AllocNewStatement(stmt_return, &result);
        MetaCParser_Match(self, tok_kw_return);
        if (MetaCParser_PeekMatch(self, tok_semicolon, true))
        {
            return_->Expression = (metac_expression_t*)_emptyPointer;
        }
        else
        {
            return_->Expression = MetaCParser_ParseExpression(self, expr_flags_none, 0);
        }
    }
    else if (tokenType == tok_kw_yield)
    {
        stmt_yield_t* yield_ = AllocNewStatement(stmt_yield, &result);
        MetaCParser_Match(self, tok_kw_yield);
        yield_->Expression = MetaCParser_ParseExpression(self, expr_flags_none, 0);
    }
    else if (tokenType == tok_lBrace)
    {
        result = (metac_statement_t*)MetaCParser_ParseBlockStatement(self, parent, prev);
    }
    else if (IsTypeToken(tokenType))
    {
        metac_token_t* peek2 = MetaCParser_PeekToken(self, 2);
        if (peek2 && IsTypeToken(peek2->TokenType))
        {
            metac_declaration_t* decl = MetaCParser_ParseDeclaration(self, 0);
            stmt_decl_t* declStmt = AllocNewStatement(stmt_decl, &result);
            declStmt->Declaration = decl;
            //result = MetaCParser_ParseDeclarationStatement(self, parent);
        }
    }

    // if we didn't parse as a declaration try an expression as the last resort
    if (!result || result == emptyPointer)
    {
        metac_expression_t* exp = MetaCParser_ParseExpression(self, expr_flags_none, 0);
        stmt_exp_t* expStmt = AllocNewStatement(stmt_exp, &result);
        expStmt->Expression = exp;
        //result = MetaCParser_ParseExpressionStatement(self, parent);
    }
LdoneWithStatement:
    if (prev)
        prev->Next = result;

    if(tokenType != tok_lBrace && MetaCParser_PeekMatch(self, tok_semicolon, true))
    {
        // XXX it shouldn't stay this way ... but for now we want
        // to parse more function bodies.
        MetaCParser_Match(self, tok_semicolon);
    }


    return result;
}

static inline void MetaCParser_PushBlockStatement(metac_parser_t* self,
												  stmt_block_t* stmt)
{
    self->CurrentBlockStatement =
        self->BlockStatementStack[self->BlockStatementStackSize++] = stmt;
}

static inline void MetaCParser_PopBlockStatement(metac_parser_t* self,
                                                 stmt_block_t* stmt)
{
    assert(stmt == self->CurrentBlockStatement);
    if (self->BlockStatementStackSize-- > 1)
        self->CurrentBlockStatement =
            self->BlockStatementStack[self->BlockStatementStackSize];
    else
        self->CurrentBlockStatement = 0;
}

static stmt_block_t* MetaCParser_ParseBlockStatement(metac_parser_t* self,
                                                     metac_statement_t* parent,
                                                     metac_statement_t* prev)
{
    MetaCParser_Match(self, tok_lBrace);

    metac_statement_t* firstStatement = 0;
    metac_statement_t* nextStatement = 0;
    stmt_block_t* result = AllocNewStatement(stmt_block, &result);

    MetaCParser_PushBlockStatement(self, result);

    for (;;)
    {
        metac_token_t* peekToken = MetaCParser_PeekToken(self, 1);

        if (peekToken && peekToken->TokenType == tok_rBrace)
        {
            if (!firstStatement)
            {
                firstStatement = (metac_statement_t*)_emptyPointer;
            }
            break;
        }

        if (!firstStatement)
        {
            firstStatement = MetaCParser_ParseStatement(self, (metac_statement_t*)result, firstStatement);
            nextStatement = firstStatement;
            if (nextStatement)
            {
                result->Hash = Mix(result->Hash, nextStatement->Hash);
            }
            else
            {
                ParseError(self->LexerState, "Statement expected");
            }
        }
        else
        {
            MetaCParser_ParseStatement(self, (metac_statement_t*)result, nextStatement);
            result->Hash = Mix(result->Hash, nextStatement->Hash);
            if (nextStatement->Next && nextStatement->Next != emptyPointer)
            {
                nextStatement = nextStatement->Next;
            }
        }
    }

    result->Body = firstStatement;

    MetaCParser_Match(self, tok_rBrace);

    MetaCParser_PopBlockStatement(self, result);

    return result;
}
/// static lexer for using in the g_lineParser
static metac_lexer_t g_lineLexer = {
    g_lineLexer.inlineTokens,     0, ARRAY_SIZE(g_lineLexer.inlineTokens),
    {g_lineLexer.inlineLocations, 0, ARRAY_SIZE(g_lineLexer.inlineLocations)}
};
/// There can only be one LineParser as it uses static storage
metac_parser_t g_lineParser = { &g_lineLexer };

void LineLexerInit(void)
{
    g_lineParser.CurrentTokenIndex = 0;
    g_lineLexer.TokenSize = 0;
    g_lineLexer.LocationStorage.LocationSize = 0;

    ACCEL_INIT(g_lineLexer, Identifier);
    ACCEL_INIT(g_lineLexer, String);
    ACCEL_INIT(g_lineParser, Identifier);
    ACCEL_INIT(g_lineParser, String);

    if (!g_lineParser.BlockStatementStack)
    {
        g_lineParser.BlockStatementStackCapacity = 8;
        g_lineParser.BlockStatementStackSize = 0;
        g_lineParser.BlockStatementStack = (stmt_block_t**)
            malloc(sizeof(stmt_block_t**) * g_lineParser.BlockStatementStackCapacity);
    }

    if (!g_lineParser.Defines)
    {
        g_lineParser.Defines = g_lineParser.inlineDefines;
        g_lineParser.DefineCapacity = ARRAY_SIZE(g_lineParser.inlineDefines);
    }
}

metac_expression_t* MetaCParser_ParseExpressionFromString(const char* exp)
{
    assert(g_lineLexer.TokenCapacity == ARRAY_SIZE(g_lineLexer.inlineTokens));
    LineLexerInit();
    LexString(&g_lineLexer, exp);

    metac_expression_t* result = MetaCParser_ParseExpression(&g_lineParser, expr_flags_none, 0);

    return result;
}

metac_statement_t* MetaCParser_ParseStatementFromString(const char* stmt)
{
    assert(g_lineLexer.TokenCapacity == ARRAY_SIZE(g_lineLexer.inlineTokens));
    LineLexerInit();
    LexString(&g_lineLexer, stmt);

    metac_statement_t* result = MetaCParser_ParseStatement(&g_lineParser, 0, 0);

    return result;
}

metac_declaration_t* MetaCParser_ParseDeclarationFromString(const char* decl)
{
    assert(g_lineLexer.TokenCapacity == ARRAY_SIZE(g_lineLexer.inlineTokens));
    LineLexerInit();
    LexString(&g_lineLexer, decl);

    metac_declaration_t* result = MetaCParser_ParseDeclaration(&g_lineParser, 0);

    return result;
}


#include <stdio.h>


const char* MetaCExpressionKind_toChars(metac_expression_kind_t type)
{
    const char* result = 0;

#define CASE_MACRO(EXP_TYPE) \
    case EXP_TYPE : {result = #EXP_TYPE;} break;

    switch(type)
    {
        FOREACH_EXP(CASE_MACRO)
    }

    return result;

#undef CASE_MACRO
}

#include "metac_printer.h"

#  ifdef TEST_PARSER
#include "metac_printer.c"
void TestParseExprssion(void)
{
    metac_printer_t printer;
    MetaCPrinter_Init(&printer,
        &g_lineParser.IdentifierTable,
        &g_lineParser.StringTable
    );
    metac_expression_t* expr;

    expr = MetaCParser_ParseExpressionFromString("12 - 16 - 99");
    assert(!strcmp(MetaCPrinter_PrintExpression(&printer, expr), "((12 - 16) - 99)"));

    expr = MetaCParser_ParseExpressionFromString("2 * 12 + 10");
    assert(!strcmp(MetaCPrinter_PrintExpression(&printer, expr), "((2 * 12) + 10)"));

    expr = MetaCParser_ParseExpressionFromString("2 + 10 * 2");
    assert(!strcmp(MetaCPrinter_PrintExpression(&printer, expr), "(2 + (10 * 2))"));

    expr = MetaCParser_ParseExpressionFromString("a = b(c)");
    assert(!strcmp(MetaCPrinter_PrintExpression(&printer, expr), "(a = b(c))"));
}

void TestParseDeclaration(void)
{
    metac_declaration_t* decl = MetaCParser_ParseDeclarationFromString("int f(double x);");
    // assert(!strcmp(PrintDeclaration(&g_lineParser, decl, 0, 0), "int f(double x);"));
}

int main(int argc, char* argv[])
{
    TestParseExprssion();
}

#  endif
#endif // ifndef DO_NOT_COMPILE
#endif // _METAC_PARSER_C_
