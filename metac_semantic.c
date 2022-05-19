#include "metac_semantic.h"
#include <assert.h>
#include "metac_alloc_node.h"
#include "metac_target_info.h"
#include "metac_default_target_info.h"

#define AT(...)

bool IsExpressionNode(metac_node_kind_t);


static inline bool isBasicType(metac_type_kind_t typeKind)
{
    if ((typeKind >= type_void) & (typeKind <= type_unsigned_long_long))
    {
        return true;
    }
    return false;
}

static uint32_t _nodeCounter = 64;

void MetaCSemantic_Init(metac_semantic_state_t* self, metac_parser_t* parser)
{
#define INIT_TYPE_TABLE(TYPE_NAME, MEMBER_NAME, INDEX_KIND) \
    TypeTableInitImpl((metac_type_table_t*)&self->MEMBER_NAME, \
                      sizeof(metac_type_ ## TYPE_NAME ## _slot_t), \
                      type_index_## INDEX_KIND);

    FOREACH_TYPE_TABLE(INIT_TYPE_TABLE)

    self->ExpressionStackCapacity = 64;
    self->ExpressionStackSize = 0;
    self->ExpressionStack = calloc(sizeof(metac_expression_t), self->ExpressionStackCapacity);

    IdentifierTableInit(&self->SemanticIdentifierTable);
    self->ParserIdentifierTable = &parser->IdentifierTable;

    self->CurrentScope = 0;
    self->CurrentDeclarationState = 0;

    MetaCPrinter_Init(&self->Printer, &self->SemanticIdentifierTable, &parser->StringTable);
}

#define POP_SCOPE(SCOPE) \
    SCOPE = SCOPE->Parent;

metac_sema_statement_t* MetaCSemantic_doStatementSemantic(metac_semantic_state_t* self,
                                                          metac_statement_t* stmt)
{
    metac_sema_statement_t* result;
    switch (stmt->StmtKind)
    {
        case stmt_exp:
        {
            sema_stmt_exp_t* sse = AllocNewSemaStatement(stmt_exp, &result);
        } break;

        default: assert(0);

        case stmt_block:
        {
            stmt_block_t* blockStatement = (stmt_block_t*) stmt;
            uint32_t statementCount = blockStatement->StatementCount;
            sema_stmt_block_t* semaBlockStatement =
                AllocNewSemaStatement(stmt_block, &result);

            metac_scope_parent_t parent = {SCOPE_PARENT_V(scope_parent_stmt,
                                           BlockStatementIndex(semaBlockStatement))};

            self->CurrentScope = MetaCScope_PushScope(self->CurrentScope, parent);
            metac_statement_t* currentStatement = blockStatement->Body;
            for(int i = 0;
                i < statementCount;
                i++)
            {
                ((&semaBlockStatement->Body)[i]) =
                    MetaCSemantic_doStatementSemantic(self, currentStatement);
                currentStatement = currentStatement->Next;
            }
            POP_SCOPE(self->CurrentScope);
        } break;

        case stmt_return:
        {
            stmt_return_t* returnStatement = (stmt_return_t*) stmt;
            sema_stmt_return_t* semaReturnStatement =
                AllocNewSemaStatement(stmt_return, &result);

            metac_sema_expression_t* returnValue =
                MetaCSemantic_doExprSemantic(self, returnStatement->Expression);
            semaReturnStatement->Expression = returnValue;
        } break;
    }

    return result;
}

metac_type_index_t MetaCSemantic_GetTypeIndex(metac_semantic_state_t* state,
                                              metac_type_kind_t typeKind,
                                              decl_type_t* type)
{
    metac_type_index_t result = {0};

    if (isBasicType(typeKind))
    {
        result.v = TYPE_INDEX_V(type_index_basic, (uint32_t) typeKind);

        assert((type == emptyPointer) || type->TypeKind == typeKind);
        if ((type != emptyPointer) && (type->TypeModifiers & typemod_unsigned))
        {
            if((typeKind >= type_char) & (typeKind <= type_long))
            {
                result.v +=
                    (((uint32_t)type_unsigned_char) - ((uint32_t)type_char));
            }
            else if (typeKind == type_long_long)
            {
                result.v +=
                    (((uint32_t)type_unsigned_long_long) - ((uint32_t)type_long_long));
            }
            else
            {
                //TODO Real error macro
                fprintf(stderr, "modifier unsigned cannot be applied to: %s\n", TypeToChars(state, result));
            }
        }
    }

    return result;
}

bool isAggregateType(metac_type_kind_t TypeKind)
{
    if (TypeKind == type_struct || TypeKind == type_union)
    {
        return true;
    }
    return false;
}
#define INVALID_SIZE ((uint32_t)-1)
#ifndef U32
#define U32(VAR) \
	(*(uint32_t*)(&VAR))
#endif
/// Returns size in byte or INVALID_SIZE on error
uint32_t MetaCSemantic_GetTypeSize(metac_semantic_state_t* self,
                                   metac_type_index_t typeIndex)
{
    uint32_t result = INVALID_SIZE;

    if (TYPE_INDEX_KIND(typeIndex) == type_index_basic)
    {
        uint32_t idx = TYPE_INDEX_INDEX(typeIndex);

        if ((idx >= type_unsigned_char)
         && (idx <= type_unsigned_long))
        {
            idx -= ((uint32_t)type_char - (uint32_t)type_unsigned_char);
        }
        else if (idx == type_unsigned_long_long)
        {
            idx = type_long_long;
        }
        result =
            MetaCTargetInfo_GetBasicSize(&default_target_info, (basic_type_kind_t) idx);
    }
    else
    {
        assert(0);
    }

    return result;
}

metac_type_index_t MetaCSemantic_doTypeSemantic(metac_semantic_state_t* self,
                                                decl_type_t* type)
{
    metac_type_index_t result = {0};

    const metac_type_kind_t typeKind = type->TypeKind;

    if (isBasicType(typeKind))
    {
        result = MetaCSemantic_GetTypeIndex(self, typeKind, type);
    }
    else if (isAggregateType(typeKind))
    {
        decl_type_struct_t* agg = (decl_type_struct_t*) type;
        sema_type_aggregate_t* semaAgg = AllocNewAggregate(typeKind);
        metac_type_aggregate_field_t* semaFields =
            AllocAggregateFields(semaAgg, typeKind, agg->FieldCount);

        metac_type_aggregate_field_t* onePastLast =
            semaFields + agg->FieldCount;
        switch(typeKind)
        {
            case type_struct:
            {
                decl_field_t* declField = agg->Fields;
                uint32_t currentFieldOffset = 0;
                uint32_t alignment = 1;
                for(metac_type_aggregate_field_t* semaField = semaFields;
                    semaField < onePastLast;
                    semaField++)
                {
                    metac_type_index_t fieldTypeIndex =
                        MetaCSemantic_doTypeSemantic(self, declField->Field.VarType);
                    uint32_t size = MetaCSemantic_GetTypeSize(self, fieldTypeIndex);
                    if (size > alignment)
                        alignment = size;
                    if (size < alignment)
                        size = alignment;
                    assert(size != INVALID_SIZE);
                    semaField->Offset = currentFieldOffset;
                    currentFieldOffset += size;
                    declField = declField->Next;
                }
                fprintf(stderr, "sizeof(struct) = %u\n", currentFieldOffset);//DEBUG
            } break;

            case type_union:
            {

            } break;

            case type_class:
            {
                assert(0);
                // Not implemented yet
            } break;

            default: assert(0);

        }
    }
    else if (type->TypeIdentifier.v)
    {
        printf("Type: %s\n", IdentifierPtrToCharPtr(self->ParserIdentifierTable, type->TypeIdentifier));
        //self->ParserIdentifierTable->
    }
    else
    {

    }



    return result;
}

void MetaCSemantic_doParameterSemantic(metac_semantic_state_t* self,
                                       sema_decl_function_t* func,
                                       sema_decl_variable_t *result,
                                       decl_parameter_t* param)
{
    uint32_t paramIndex = result - func->Parameters;

    result->VarIdentifier = param->Identifier;
    result->VarType = MetaCSemantic_doTypeSemantic(self, param->Type);
    result->VarInitExpression = 0;
}
#include "crc32c.h"

sema_decl_function_t* MetaCSemantic_doFunctionSemantic(metac_semantic_state_t* self,
                                                       decl_function_t* func)
{
    // one cannot do nested function semantic at this point
    // assert(self->CurrentDeclarationState == 0);
    metac_sema_decl_state_t declState = {0};
    self->CurrentDeclarationState = &declState;

    sema_decl_function_t* f =
        AllocNewSemaFunction(func);
    // let's first do the parameters
    sema_decl_variable_t* params = f->Parameters =
        AllocFunctionParameters(f, func->ParameterCount);

    decl_parameter_t* currentParam = func->Parameters;
    for(int i = 0;
        i < func->ParameterCount;
        i++)
    {
        MetaCSemantic_doParameterSemantic(self, f,
                                          params + i,
                                          currentParam);

        currentParam = currentParam->Next;
    }

    assert(currentParam == emptyPointer);
    metac_scope_parent_t Parent = {SCOPE_PARENT_V(scope_parent_function, FunctionIndex(f))};

    self->CurrentScope = f->Scope = MetaCScope_PushScope(self->CurrentScope, Parent);
    // now we have to add the parameters to the scope.
    sema_decl_variable_t p3[3] = { f->Parameters[0], f->Parameters[1], f->Parameters[2] };

    for(uint32_t i = 0;
        i < func->ParameterCount;
        i++)
    {
        //TODO we cannot hash parameter and variable names all the time
        char* idChars = self->ParserIdentifierTable->StringMemory +
                        (params[i].VarIdentifier.v - 4);
        uint32_t idLen = strlen(idChars);
        uint32_t key   = IDENTIFIER_KEY(crc32c(~0, idChars, idLen), idLen);
        metac_node_header_t* ptr = (metac_node_header_t*)(&f->Parameters[i]);
        scope_insert_error_t result =
            MetaCScope_RegisterIdentifier(f->Scope, params[i].VarIdentifier,
                                          (metac_node_header_t*)(ptr));
        assert(MetaCScope_LookupIdentifier(f->Scope, params[i].VarIdentifier)
                == params + i);


    }

    f->FunctionBody = (sema_stmt_block_t*)
        MetaCSemantic_doStatementSemantic(self, (metac_statement_t*)func->FunctionBody);

    POP_SCOPE(self->CurrentScope);

    return f;
}

metac_sema_declaration_t* MetaCSemantic_doDeclSemantic(metac_semantic_state_t* self,
                                                       metac_declaration_t* decl)
{
    metac_sema_declaration_t* result;

    switch(decl->DeclKind)
    {
        case decl_function:
        {
            decl_function_t* f = cast(decl_function_t*) decl;
            result = (metac_sema_declaration_t*)
                MetaCSemantic_doFunctionSemantic(self, f);

        } break;
        case decl_parameter:
            assert(0);
        case decl_variable:
        {
            decl_variable_t* v = cast(decl_variable_t*) decl;
        } break;
        case decl_type_struct:
            ((decl_type_t*)decl)->TypeKind = type_struct;
            goto LdoTypeSemantic;
        case decl_type_union:
            ((decl_type_t*)decl)->TypeKind = type_union;
            goto LdoTypeSemantic;
        case decl_type_array:
            ((decl_type_t*)decl)->TypeKind = type_array;
            goto LdoTypeSemantic;
        case decl_typedef:
            ((decl_type_t*)decl)->TypeKind = type_typedef;
            goto LdoTypeSemantic;
    LdoTypeSemantic:
        {
            metac_type_index_t type_index =
                MetaCSemantic_doTypeSemantic(self, (decl_type_t*)decl);
        } break;
    }
}

#ifndef ATOMIC
#define INC(v) \
    (v++)
#else
#define INC(v)
    (__builtin_atomic_fetch_add(&v, __ATOMIC_RELEASE))
#endif

metac_type_index_t MetaCSemantic_GetArrayTypeOf(metac_semantic_state_t* state,
                                                metac_type_index_t elementTypeIndex,
                                                uint32_t dimension)
{
    uint32_t hash = EntangleInts(TYPE_INDEX_INDEX(elementTypeIndex), dimension);
    metac_type_array_slot_t key = {hash, elementTypeIndex, dimension};

    metac_type_index_t result =
        MetaCTypeTable_GetOrAddArrayType(&state->ArrayTypeTable, hash, &key);

    return result;
}

metac_type_index_t MetaCSemantic_GetPtrTypeOf(metac_semantic_state_t* state,
                                              metac_type_index_t elementTypeIndex)
{
    uint32_t hash = elementTypeIndex.v;
    metac_type_ptr_slot_t key = {hash, elementTypeIndex};

    metac_type_index_t result =
        MetaCTypeTable_GetOrAddPtrType(&state->PtrTypeTable, hash, &key);

    return result;
}

#include "metac_printer.h"
static inline const char* BasicTypeToChars(metac_type_index_t typeIndex)
{
    assert(TYPE_INDEX_KIND(typeIndex) == type_index_basic);
    switch((metac_type_kind_t) TYPE_INDEX_INDEX(typeIndex))
    {
        case type_invalid :
            assert(0);

        case type_void :
            return "void";

        case type_bool :
            return "bool";
        case type_char:
            return "char";
        case type_short:
            return "short";
        case type_int :
            return "int";
        case type_long :
            return "long";
        case type_size_t:
            return "size_t";
        case type_long_long:
            return "long long";

        case type_float :
            return "float";
        case type_double :
            return "double";

        case type_unsigned_char:
            return "unsigned char";
        case type_unsigned_short:
            return "unsigned short";
        case type_unsigned_int:
            return "unsigned int";
        case type_unsigned_long :
            return "unsigned long";
        case type_unsigned_long_long:
            return "unsigned long long";


        default: assert(0);
    }
    return 0;
}
#ifndef _emptyPointer
#define _emptyPointer (void*)0x1
#define emptyNode (metac_node_header_t*) _emptyPointer
#endif


/// Returns _emptyNode to signifiy it could not be found
/// a valid node otherwise
metac_node_header_t* MetaCSemantic_LookupIdentifier(metac_semantic_state_t* self,
                                                    metac_identifier_ptr_t identifierPtr)
{
    metac_node_header_t* result = emptyNode;
    metac_scope_t *currentScope = self->CurrentScope;
#if 1
    if (!currentScope && self->declStore)
    {
        metac_identifier_ptr_t dStoreIdPtr =
            FindMatchingIdentifier(&self->declStore->Table,
                                   self->ParserIdentifierTable,
                                   identifierPtr);
        if (dStoreIdPtr.v)
        {
            metac_declaration_t* decl =
                DeclarationStore_GetDecl(self->declStore, dStoreIdPtr);
        }
    }
    else
#endif
    {
        while(currentScope)
        {
            metac_node_header_t* lookupResult =
                MetaCScope_LookupIdentifier(currentScope, identifierPtr);
            if (lookupResult)
            {
                result = lookupResult;
                break;
            }
            POP_SCOPE(currentScope);
        }
    }
    return result;
}

static inline void TypeToCharsP(metac_semantic_state_t* self,
                                metac_printer_t* printer,
                                metac_type_index_t typeIndex)
{
    uint32_t typeIndexIndex = TYPE_INDEX_INDEX(typeIndex);

    switch (TYPE_INDEX_KIND(typeIndex))
    {
        case type_index_array:
        {
            metac_type_array_slot_t* arrayType =
                (self->ArrayTypeTable.Slots + TYPE_INDEX_INDEX(typeIndex));
            TypeToCharsP(self, printer, arrayType->ElementTypeIndex);
            MetacPrinter_PrintStringLiteral(printer, "[");
            MetacPrinter_PrintI64(printer, (int64_t)arrayType->Dimension);
            MetacPrinter_PrintStringLiteral(printer, "]");
        } break;
        case type_index_basic:
        {
            const char* typeString = BasicTypeToChars(typeIndex);
            MetacPrinter_PrintStringLiteral(printer, typeString);
        } break;
        case type_index_ptr:
        {
            metac_type_ptr_slot_t* ptrType =
                (self->PtrTypeTable.Slots + TYPE_INDEX_INDEX(typeIndex));
            TypeToCharsP(self, printer, ptrType->ElementTypeIndex);
            MetacPrinter_PrintStringLiteral(printer, "*");
        } break;
    }
}

const char* TypeToChars(metac_semantic_state_t* self, metac_type_index_t typeIndex)
{
    const char* result = 0;
    static metac_printer_t printer = {0};
    if (!printer.StringMemory)
        MetaCPrinter_InitSz(&printer, self->ParserIdentifierTable, 0, 32);
    else
        MetaCPrinter_Reset(&printer);
    TypeToCharsP(self, &printer, typeIndex);
    printer.StringMemory[printer.StringMemorySize++] = '\0';
    result = printer.StringMemory;

    return result;
}

void MetaCSemantic_PushExpr(metac_semantic_state_t* self, metac_sema_expression_t* expr)
{
    if (self->ExpressionStackCapacity < self->ExpressionStackSize)
    {
        assert(0);
        // we would need to realloc in this case.
    }
}

void MetaCSemantic_PopExpr(metac_semantic_state_t* self,  metac_sema_expression_t* expr)
{

}

bool MetaCSemantic_CanHaveAddress(metac_semantic_state_t* self,
                                  metac_expression_t* expr)
{
    switch (expr->Kind)
    {
        case exp_identifier:
            return true;
        default: return false;
    }
}
#include <stdio.h>


#undef offsetof

#define offsetof(st, m) \
    ((size_t)((char *)&((st *)0)->m - (char *)0))

metac_sema_expression_t* MetaCSemantic_doExprSemantic(metac_semantic_state_t* self,
                                                      metac_expression_t* expr)
{
    metac_sema_expression_t* result = 0;

    result = AllocNewSemaExpression(expr);

    if (IsBinaryExp(expr->Kind))
    {
        MetaCSemantic_PushExpr(self, result);

        MetaCSemantic_doExprSemantic(self, expr->E1);
        MetaCSemantic_doExprSemantic(self, expr->E2);

        MetaCSemantic_PopExpr(self, result);
    }

    switch(expr->Kind)
    {
        case exp_invalid:
            assert(0);

        case exp_char :
            result->TypeIndex = MetaCSemantic_GetTypeIndex(self, type_char, emptyPointer);
        break;
        case exp_string :
            result->TypeIndex = MetaCSemantic_GetArrayTypeOf(self,
                MetaCSemantic_GetTypeIndex(self, type_char, _emptyPointer),
                LENGTH_FROM_STRING_KEY(expr->StringKey));
        break;
        case exp_signed_integer :
            result->TypeIndex = MetaCSemantic_GetTypeIndex(self, type_int, emptyPointer);
        break;
        case exp_sizeof:
        {
            uint32_t size = -1;
            metac_type_index_t type = MetaCSemantic_doTypeSemantic(self, expr->SizeofType);
            size = MetaCSemantic_GetTypeSize(self, type);
            result->TypeIndex.v = TYPE_INDEX_V(type_index_basic, type_size_t);
            result->Kind = exp_signed_integer;
            result->ValueU64 = size;
        } break;
        case exp_identifier:
        {
            metac_node_header_t* node =
                MetaCSemantic_LookupIdentifier(self,
                                               result->IdentifierPtr);
            if (IsExpressionNode(node->Kind))
            {
                result = (metac_sema_expression_t*) node;
                if (node->Kind == (metac_expression_kind_t)exp_identifier)
                {
                    fprintf(stderr, "Identifier lookup failed\n");
                }
            }
            //assert(0);
            //
        }
        break;
        case exp_addr:
            MetaCSemantic_PushExpr(self, result);
            result->E1 = MetaCSemantic_doExprSemantic(self, expr->E1);
            MetaCSemantic_PopExpr(self, result);
            assert(result->E1->TypeIndex.v != 0 && result->E1->TypeIndex.v != ERROR_TYPE_INDEX_V);
            if (!MetaCSemantic_CanHaveAddress(self, expr->E1))
            {
                result->TypeIndex.v = ERROR_TYPE_INDEX_V;
                SemanticError(self, "cannot take the address of %s", MetaCPrinter_PrintExpression(&self->Printer, expr->E1));
            }
            else
            {
                result->TypeIndex = MetaCSemantic_GetPtrTypeOf(self, result->E1->TypeIndex);
            }
        break;
    }

    return result;
}
