#ifndef _METAC_PRINTER_H_
#define _METAC_PRINTER_H_
#pragma once

#include "compat.h"
#include "metac_identifier_table.h"
#include "metac_parser.h"

#define CHUNK_SIZE 4096
#define INITIAL_SIZE (16 * 4096)
#define GROWTH_FACTOR 1.3f

typedef struct metac_printer_t
{
    char* StringMemory;
    uint32_t StringMemorySize;
    uint32_t StringMemoryCapacity;
    
    metac_identifier_table_t* IdentifierTable;
    metac_identifier_table_t* StringTable;
    
    uint16_t IndentLevel;
    uint16_t CurrentColumn;
    
    uint16_t StartColumn;
} metac_printer_t;


void MetaCPrinter_Init(metac_printer_t* self,
                       metac_identifier_table_t* identifierTable,
                       metac_identifier_table_t* stringTable);

const char* MetaCPrinter_PrintExpression(metac_printer_t* self, metac_expression_t* exp);
const char* MetaCPrinter_PrintDeclaration(metac_printer_t* self, metac_declaration_t* decl);
const char* MetaCPrinter_PrintStatement(metac_printer_t* self, metac_statement_t* stmt);

void MetaCPrinter_Reset(metac_printer_t* self);

#endif // _METAC_PRINTER_H_