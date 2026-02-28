/*
 * The MIT License
 *
 * Copyright 2020 The OpenNARS authors.
 */

#ifndef H_NARSESE
#define H_NARSESE

/////////////////////
// Narsese encoder //
/////////////////////

#include <string.h>
#include <stdio.h>
#include "Term.h"
#include "Globals.h"
#include "Config.h"
#include "HashTable.h"

#define Narsese_RuleTableVars "ABCMRSPXYZ"
#define Narsese_CanonicalCopulas "@*&|;:=$'\"/\\.-%#~+!?^_,"
#define PRODUCT '*'
#define EXT_INTERSECTION '&'
#define INT_INTERSECTION '|'
#define CONJUNCTION ';'
#define INHERITANCE ':'
#define SIMILARITY '='
#define TEMPORAL_IMPLICATION '$'
#define INT_SET '\''
#define EXT_SET '"'
#define EXT_IMAGE1 '/'
#define INT_IMAGE1 '\\'
#define SET_ELEMT '.'
#define EXT_DIFFERENCE '-'
#define EXT_IMAGE2 '%'
#define INT_IMAGE2 '#'
#define INT_DIFFERENCE '~'
#define SEQUENCE '+'
#define NEGATION '!'
#define IMPLICATION '?'
#define EQUIVALENCE '^'
#define DISJUNCTION '_'
#define HAS_CONTINUOUS_PROPERTY ','
#define SET_TERMINATOR '@'

void Narsese_INIT(NAR_t *nar);
char* Narsese_Expand(NAR_t *nar, char *narsese);
char** Narsese_PrefixTransform(NAR_t *nar, char* narsese_expanded);
Term Narsese_Term(NAR_t *nar, char *narsese);
int  Narsese_Sentence(NAR_t *nar, char *narsese, Term *destTerm, char *punctuation, int *tense, Truth *destTv, double *occurrenceTimeOffset);
Term Narsese_Sequence(NAR_t *nar, Term *a, Term *b, bool *success);
Term Narsese_AtomicTerm(NAR_t *nar, char *name);
int Narsese_AtomicTermIndex(NAR_t *nar, char *name);
int Narsese_CopulaIndex(NAR_t *nar, char name);
void Narsese_PrintAtom(NAR_t *nar, Atom atom);
void Narsese_PrintTerm(NAR_t *nar, Term *term);
bool Narsese_copulaEquals(NAR_t *nar, Atom atom, char name);
bool Narsese_isOperator(NAR_t *nar, Atom atom);
Atom Narsese_getOperationAtom(NAR_t *nar, Term *term);
Term Narsese_getOperationTerm(NAR_t *nar, Term *term);
bool Narsese_isOperation(NAR_t *nar, Term *term);
bool Narsese_isExecutableOperation(NAR_t *nar, Term *term);
Term Narsese_GetPreconditionWithoutOp(NAR_t *nar, Term *precondition);
bool Narsese_IsSimpleAtom(NAR_t *nar, Atom atom);
bool Narsese_HasSimpleAtom(NAR_t *nar, Term *term);
bool Narsese_HasOperation(NAR_t *nar, Term *term);
bool Narsese_StringEqual(char *name1, char *name2);
Hash_t Narsese_StringHash(char *name);
bool Narsese_OperationSequenceAppendLeftNested(NAR_t *nar, Term *start, Term *sequence);
void Narsese_setAtomValue(NAR_t *nar, Atom atom, double value, char* measurementName);
bool Narsese_hasAtomValue(NAR_t *nar, Atom atom);
double Narsese_getAtomValue(NAR_t *nar, Atom atom);
int Narsese_SequenceLength(NAR_t *nar, Term *sequence);
int Narsese_CountAtomsUsed(NAR_t *nar);

#endif
