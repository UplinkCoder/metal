#include "../metal_lexer.h"
#include "../metal_parser.h"
#include "../3rd_party/linenoise/linenoise.c"
#include "../int_to_str.c"

const char* MetalTokenEnum_toChars(metal_token_enum_t tok);

int main(int argc, const char* argv[])
{
    const char* line;

    metal_lexer_state_t repl_state = {0, 0, 0, 0};
    metal_lexer_t lexer;
    InitMetalLexer(&lexer);
    metal_parser_t parser;
    MetalParserInitFromLexer(&parser, &lexer);

    while ((line = linenoise("REPL>")))
    {
        linenoiseHistoryAdd(line);
        uint32_t line_length = strlen(line);
        if (*line == ':' && (*(line + 1) == 'q'))
            return 0;

        while (line_length > 0)
        {
            uint32_t initalPosition = repl_state.Position;
            metal_token_t token =
                *MetalLexerLexNextToken(&lexer, &repl_state, line, line_length);

            uint32_t eaten_chars = repl_state.Position - initalPosition;
            const uint32_t token_length = MetalTokenLength(token);
#if 1
            printf("read tokenType: %s {length: %d}\n",
                    MetalTokenEnum_toChars(token.TokenType), token_length);

            if (token.TokenType == tok_identifier)
            {
                printf("    %.*s\n", token.Length, token.Identifier);
            }
            else if (token.TokenType == tok_unsignedNumber)
            {
                char buffer[21];
                printf("    %s\n", u64tostr(token.ValueU64, buffer));
            }
            else if (token.TokenType == tok_stringLiteral)
            {
                printf("    \"%.*s\"\n", token.Length, token.String);
            }
#endif
            line_length -= eaten_chars;
            line += eaten_chars;

            if (!token_length)
                break;
        }
    }
}
