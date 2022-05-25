#ifndef _METAC_TYPE_TABLE_H_
#define _METAC_TYPE_TABLE_H_

#include "compat.h"
#include "metac_type.h"
#include <assert.h>

uint32_t EntangleInts(uint32_t a, uint32_t b);
uint32_t UntangleInts(uint32_t tangled);

typedef struct metac_type_table_slot_t
{
    uint32_t HashKey;
} metac_type_table_slot_t;

typedef struct metac_type_enum_slot_t
{
    uint32_t HashKey;

    uint32_t MemberCount;
    metac_enum_member_t* Members;
} metac_type_enum_slot_t;

typedef struct metac_type_aggregate_slot_t
{
    uint32_t HashKey;

    uint32_t fieldCount;
    metac_type_aggregate_field_t* fields;
} metac_type_aggregate_slot_t;

typedef struct metac_type_functiontype_slot_t
{
    uint32_t HashKey;

    metac_type_index_t Type;
} metac_type_functiontype_slot_t;

typedef struct metac_type_ptr_slot_t
{
    uint32_t HashKey;

    metac_type_index_t ElementTypeIndex;
} metac_type_ptr_slot_t;


typedef struct metac_type_array_slot_t
{
    uint32_t HashKey;

    metac_type_index_t ElementTypeIndex;
    uint32_t Dimension;
} metac_type_array_slot_t;

typedef struct metac_type_typedef_slot_t
{
    uint32_t HashKey;

    metac_type_index_t ElementTypeIndex;
} metac_type_typedef_slot_t;

typedef struct  metac_type_table_t
{
    metac_type_table_slot_t* Slots;

    uint32_t SlotCount_Log2;
    uint32_t SlotsUsed;

    uint32_t MaxDisplacement;
    metac_type_index_kind_t Kind;
} metac_type_table_t;


#define METAC_TYPE_TABLE_T(SLOT_TYPE) \
    metac_type_table_##SLOT_TYPE##_t

#define METAC_TYPE_TABLE_KEY_T(SLOT_TYPE) \
    metac_type_## SLOT_TYPE ##_slot_t

#define METAC_TYPE_TABLE_T_DEF(SLOT_TYPE) \
typedef struct  METAC_TYPE_TABLE_T(SLOT_TYPE) \
{ \
    metac_type_## SLOT_TYPE ##_slot_t* Slots; \
    \
    uint32_t SlotCount_Log2; \
    uint32_t SlotsUsed; \
    \
    uint32_t MaxDisplacement; \
    metac_type_index_kind_t Kind; \
} METAC_TYPE_TABLE_T(SLOT_TYPE);

#define FOREACH_TABLE_SLOT_TYPE(M) \
    M(enum) \
    M(array) \
    M(aggregate) \
    M(ptr) \
    M(functiontype) \
    M(typedef)

FOREACH_TABLE_SLOT_TYPE(METAC_TYPE_TABLE_T_DEF)


#define FOREACH_TABLE_MEMBER(M) \
    M(enum, Enum) \
    M(array, Array) \
    M(aggregate, Struct) \
    M(aggregate, Union) \
    M(ptr, Ptr) \
    M(functiontype, Function) \
    M(typedef, Typedef)

#define DECLARE_GET_OR_ADD(TYPE_NAME, MEMBER_NAME) \
    metac_type_index_t MetaCTypeTable_GetOrAdd ## MEMBER_NAME ## Type \
    (METAC_TYPE_TABLE_T(TYPE_NAME)* table, \
     uint32_t hash, METAC_TYPE_TABLE_KEY_T(TYPE_NAME)* key);

FOREACH_TABLE_MEMBER(DECLARE_GET_OR_ADD)
/*
metac_type_index_t MetaCTypeTable_GetOrAddArrayType(METAC_TYPE_TABLE_T(array)* table,
                                                    uint32_t hash,
                                                    metac_type_array_slot_t* key);
*/
//void MetaCTypeTable_

void TypeTableInitImpl(metac_type_table_t* table, const uint32_t sizeof_slot, metac_type_index_kind_t kind);
#endif
