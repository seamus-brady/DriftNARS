/*
 * The MIT License
 *
 * Copyright 2020 The OpenNARS authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "NAL.h"
#include "NAR.h"

/* ruleID is only used during single-threaded build-time code generation */
static int ruleID = 0;

/* nal_gen_nar is set only during NAL_GenerateRuleTable (code-gen phase) */
static NAR_t *nal_gen_nar = NULL;

static void NAL_GeneratePremisesUnifier(int i, Atom atom, int premiseIndex)
{
    if(atom)
    {
        bool isOp = nal_gen_nar->atom_names[atom-1][0] == 'O' && nal_gen_nar->atom_names[atom-1][1] == 'p';
        //upper case atoms are treated as variables in the meta rule language
        if(nal_gen_nar->atom_names[atom-1][0] >= 'A' && nal_gen_nar->atom_names[atom-1][0] <= 'Z')
        {
            //unification failure by inequal value assignment (value at position i versus previously assigned one), and variable binding
            printf("subtree = Term_ExtractSubterm(&term%d, %d);\n", premiseIndex, i);
            printf("if((substitutions[%d].atoms[0]!=0 && !Term_Equal(&substitutions[%d], &subtree)) || Narsese_copulaEquals(nar, subtree.atoms[0], SET_TERMINATOR)){ goto RULE_%d; }\n", atom, atom, ruleID);
            if(isOp)
            {
                printf("if(!Narsese_isOperation(nar, &subtree)) { goto RULE_%d; }\n", ruleID);
            }
            printf("substitutions[%d] = subtree;\n", atom);
        }
        else
        {
            //structural constraint given by copulas at position i
            printf("if(term%d.atoms[%d] != %d){ goto RULE_%d; }\n", premiseIndex, i, atom, ruleID);
        }
    }
}

static void NAL_GenerateConclusionSubstitution(int i, Atom atom)
{
    if(atom)
    {
        if(nal_gen_nar->atom_names[atom-1][0] >= 'A' && nal_gen_nar->atom_names[atom-1][0] <= 'Z')
        {
            //conclusion term gets variables substituted
            printf("if(!Term_OverrideSubterm(&conclusion,%d,&substitutions[%d])){ goto RULE_%d; }\n", i, atom, ruleID);
        }
        else
        {
            //conclusion term inherits structure from meta rule, namely the copula
            printf("conclusion.atoms[%d] = %d;\n", i, atom);
        }
    }
}

static void NAL_GenerateConclusionTerm(char *premise1, char *premise2, char* conclusion, bool doublePremise)
{
    Term term1 = Narsese_Term(nal_gen_nar, premise1);
    Term term2 = doublePremise ? Narsese_Term(nal_gen_nar, premise2) : (Term) {0};
    Term conclusion_term = Narsese_Term(nal_gen_nar, conclusion);
    printf("RULE_%d:\n{\n", ruleID++);
    //skip double/single premise rule if single/double premise
    if(doublePremise) { printf("if(!doublePremise) { goto RULE_%d; }\n", ruleID); }
    if(!doublePremise) { printf("if(doublePremise) { goto RULE_%d; }\n", ruleID); }
    puts("Term substitutions[27+NUM_ELEMENTS(Narsese_RuleTableVars)+2] = {0}; Term subtree = {0};"); //27 because of 9 indep, 9 dep, 9 query vars, and +1 for Op1 and Op2
    for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
    {
        NAL_GeneratePremisesUnifier(i, term1.atoms[i], 1);
    }
    if(doublePremise)
    {
        for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
        {
            NAL_GeneratePremisesUnifier(i, term2.atoms[i], 2);
        }
    }
    puts("Term conclusion = {0};");
    for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
    {
        NAL_GenerateConclusionSubstitution(i, conclusion_term.atoms[i]);
    }
    (void) term2; /* suppress unused-variable warning in single-premise case */
}

static void NAL_GenerateRule(char *premise1, char *premise2, char* conclusion, char* truthFunction, bool doublePremise, bool switchTruthArgs, bool VarIntro)
{
    NAL_GenerateConclusionTerm(premise1, premise2, conclusion, doublePremise);
    if(switchTruthArgs)
    {
        printf("Truth conclusionTruth = %s(truth2,truth1);\n", truthFunction);
    }
    else
    {
        printf("Truth conclusionTruth = %s(truth1,truth2);\n", truthFunction);
    }
    printf("NAL_DerivedEvent(nar, RuleTable_Reduce(nar, conclusion), conclusionOccurrence, conclusionTruth, conclusionStamp, currentTime, parentPriority, conceptPriority, occurrenceTimeOffset, validation_concept, validation_cid, %d, false, eternalize);}\n", VarIntro);
}

static void NAL_GenerateReduction(char *premise1, char* conclusion)
{
    NAL_GenerateConclusionTerm(premise1, NULL, conclusion, false);
    puts("IN_DEBUG( fputs(\"Reduced: \", stdout); Narsese_PrintTerm(nar, &term1); fputs(\" -> \", stdout); Narsese_PrintTerm(nar, &conclusion); puts(\"\"); ) \nreturn conclusion;\n}");
}

void NAL_GenerateRuleTable(NAR_t *nar)
{
    nal_gen_nar = nar;
    ruleID = 0;
    puts("#include \"RuleTable.h\"");
    puts("void RuleTable_Apply(NAR_t *nar, Term term1, Term term2, Truth truth1, Truth truth2, long conclusionOccurrence, double occurrenceTimeOffset, Stamp conclusionStamp, long currentTime, double parentPriority, double conceptPriority, bool doublePremise, Concept *validation_concept, long validation_cid, bool eternalize)\n{\ngoto RULE_0;");
#define H_NAL_RULES
#include "NAL.h"
#undef H_NAL_RULES
    printf("RULE_%d:;\n}\n", ruleID);
    printf("Term RuleTable_Reduce(NAR_t *nar, Term term1)\n{\nbool doublePremise = false;\ngoto RULE_%d;\n", ruleID);
#define H_NAL_REDUCTIONS
#include "NAL.h"
#undef H_NAL_REDUCTIONS
    printf("RULE_%d:;\nreturn term1;\n}\n\n", ruleID);
    nal_gen_nar = NULL;
}

static bool NAL_AtomAppearsTwice(NAR_t *nar, Term *conclusionTerm)
{
    //mandatory part of the filter: (same subject and predicate of implication of equivalence)
    if(Narsese_copulaEquals(nar, conclusionTerm->atoms[0], EQUIVALENCE) || Narsese_copulaEquals(nar, conclusionTerm->atoms[0], IMPLICATION))
    {
        Term t1 = Term_ExtractSubterm(conclusionTerm, 1);
        Term t2 = Term_ExtractSubterm(conclusionTerm, 2);
        if(Term_Equal(&t1, &t2))
        {
            return true;
        }
    }
    if(!ATOM_APPEARS_TWICE_FILTER)
        return false;
    if(Narsese_copulaEquals(nar, conclusionTerm->atoms[0], INHERITANCE) || Narsese_copulaEquals(nar, conclusionTerm->atoms[0], SIMILARITY)) //similarity or inheritance
    {
        //<(A * B) --> r>.
        //0   1  2 3 4
        //--> *  r A B
        if(!(Narsese_copulaEquals(nar, conclusionTerm->atoms[1], PRODUCT) && Narsese_IsSimpleAtom(nar, conclusionTerm->atoms[2]))) //relational statements can have atoms mentioned more than once
        {
            nar->nal_atomsCounter++;
            for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
            {
                Atom atom = conclusionTerm->atoms[i];
                if(nar->nal_atomsAppeared[conclusionTerm->atoms[i]] == nar->nal_atomsCounter) //atom already appeared
                {
                    return true;
                }
                if(Narsese_IsSimpleAtom(nar, atom))
                {
                    nar->nal_atomsAppeared[atom] = nar->nal_atomsCounter;
                }
            }
        }
    }
    return false;
}

static bool NAL_NestedHOLStatement(NAR_t *nar, Term *conclusionTerm)
{
    if(!NESTED_HOL_STATEMENT_FILTER)
        return false;
    //We don't allow two ==> or <=> in one statement:
    int imp_equ = 0;
    int temp_equ = 0;
    for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
    {
        if(Narsese_copulaEquals(nar, conclusionTerm->atoms[i], IMPLICATION) || Narsese_copulaEquals(nar, conclusionTerm->atoms[i], EQUIVALENCE))
        {
            imp_equ++;
        }
        if(Narsese_copulaEquals(nar, conclusionTerm->atoms[i], TEMPORAL_IMPLICATION))
        {
            temp_equ++;
        }
        if(imp_equ >= 2 || temp_equ > 2)
        {
            return true;
        }
    }
    return false;
}

static bool NAL_InhOrSimHasDepVar(NAR_t *nar, Term *conclusionTerm)
{
    if(!INH_OR_SIM_HAS_DEP_VAR_FILTER)
        return false;
    if(Narsese_copulaEquals(nar, conclusionTerm->atoms[0], INHERITANCE) ||
       Narsese_copulaEquals(nar, conclusionTerm->atoms[0], SIMILARITY))
    {
        if(Variable_hasVariable(nar, conclusionTerm, false, true, false))
        {
            return true;
        }
    }
    return false;
}

static bool NAL_HOLStatementComponentHasInvalidInhOrSim(NAR_t *nar, Term *conclusionTerm, bool firstIteration)
{
    if(!HOL_STATEMENT_COMPONENT_HAS_INVALID_INH_OR_SIM_FILTER)
        return false;
    if(Narsese_copulaEquals(nar, conclusionTerm->atoms[0], EQUIVALENCE) || Narsese_copulaEquals(nar, conclusionTerm->atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, conclusionTerm->atoms[0], CONJUNCTION) || Narsese_copulaEquals(nar, conclusionTerm->atoms[0], DISJUNCTION))
    {
        Term subject = Term_ExtractSubterm(conclusionTerm, 1);
        Term predicate = Term_ExtractSubterm(conclusionTerm, 2);
        return NAL_HOLStatementComponentHasInvalidInhOrSim(nar, &subject, false) || NAL_HOLStatementComponentHasInvalidInhOrSim(nar, &predicate, false);
    }
    if(!firstIteration && (Narsese_copulaEquals(nar, conclusionTerm->atoms[0], INHERITANCE) || Narsese_copulaEquals(nar, conclusionTerm->atoms[0], SIMILARITY)))
    {
        Term subject = Term_ExtractSubterm(conclusionTerm, 1);
        Term predicate = Term_ExtractSubterm(conclusionTerm, 2);
        if(Term_Equal(&subject, &predicate) || ((Variable_isIndependentVariable(nar, subject.atoms[0])   || Variable_isDependentVariable(nar, subject.atoms[0])) &&
                                                (Variable_isIndependentVariable(nar, predicate.atoms[0]) || Variable_isDependentVariable(nar, predicate.atoms[0]))))
        {
            return true;
        }
        if(!Variable_hasVariable(nar, conclusionTerm, true, true, false) && HOL_COMPONENT_NO_VAR_IS_INVALID_FILTER)
        {
            return true;
        }
        if(!Narsese_HasSimpleAtom(nar, conclusionTerm) && HOL_COMPONENT_NO_ATOMIC_IS_INVALID_FILTER)
        {
            return true;
        }
        bool SubjectHasProduct = Narsese_copulaEquals(nar, subject.atoms[0], PRODUCT);
        bool PredicateHasProduct = Narsese_copulaEquals(nar, predicate.atoms[0], PRODUCT);
        bool SubjectIsImage =   Narsese_copulaEquals(nar, subject.atoms[0], INT_IMAGE1) || Narsese_copulaEquals(nar, subject.atoms[0], INT_IMAGE2);
        bool PredicateIsImage = Narsese_copulaEquals(nar, predicate.atoms[0], EXT_IMAGE1) || Narsese_copulaEquals(nar, predicate.atoms[0], EXT_IMAGE2);
        if(TERMS_WITH_VARS_AND_ATOMS_FILTER  && !SubjectIsImage && !PredicateIsImage && !Narsese_copulaEquals(nar, subject.atoms[0],   SET_ELEMT) && !Narsese_copulaEquals(nar, predicate.atoms[0], SET_ELEMT) &&
           ((!SubjectHasProduct && Variable_hasVariable(nar, &subject,   true, true, false) && Narsese_HasSimpleAtom(nar, &subject)) ||
            (!PredicateHasProduct && Variable_hasVariable(nar, &predicate, true, true, false) && Narsese_HasSimpleAtom(nar, &predicate))))
        {
            return true;
        }
        if(TERMS_WITH_VARS_AND_ATOMS_FILTER && SubjectIsImage)
        {
            Term relation = Term_ExtractSubterm(&subject, 1);
            Term relata =   Term_ExtractSubterm(&subject, 2);
            if((Variable_hasVariable(nar, &relation, true, true, false) && Narsese_HasSimpleAtom(nar, &relation)) ||
               (Variable_hasVariable(nar, &relata,   true, true, false) && Narsese_HasSimpleAtom(nar, &relata)))
            {
                return true;
            }
        }
        if(TERMS_WITH_VARS_AND_ATOMS_FILTER && PredicateIsImage)
        {
            Term relation = Term_ExtractSubterm(&predicate, 1);
            Term relata =   Term_ExtractSubterm(&predicate, 2);
            if((Variable_hasVariable(nar, &relation, true, true, false) && Narsese_HasSimpleAtom(nar, &relation)) ||
               (Variable_hasVariable(nar, &relata,   true, true, false) && Narsese_HasSimpleAtom(nar, &relata)))
            {
                return true;
            }
        }
    }
    return false;
}

static bool NAL_JunctionNotRightNested(NAR_t *nar, Term *conclusionTerm)
{
    if(!JUNCTION_NOT_RIGHT_NESTED_FILTER)
    {
        return false;
    }
    for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
    {
        if(Narsese_copulaEquals(nar, conclusionTerm->atoms[i], CONJUNCTION) || Narsese_copulaEquals(nar, conclusionTerm->atoms[i], DISJUNCTION))
        {
            int i_right_child = ((i+1)*2+1)-1;
            if(i < COMPOUND_TERM_SIZE_MAX &&  (Narsese_copulaEquals(nar, conclusionTerm->atoms[i_right_child], CONJUNCTION) || Narsese_copulaEquals(nar, conclusionTerm->atoms[i_right_child], DISJUNCTION)))
            {
                return true;
            }
        }
    }
    return false;
}

static bool InvalidSetOp(NAR_t *nar, Term *conclusionTerm, Truth conclusionTruth) //to be refined, with atom appears twice restriction for now it's fine
{
    (void) conclusionTruth;
    if(Narsese_copulaEquals(nar, conclusionTerm->atoms[0], INHERITANCE))
    {
        //extensional intersection between extensional sets
        if(Narsese_copulaEquals(nar, conclusionTerm->atoms[1], EXT_INTERSECTION) && Narsese_copulaEquals(nar, conclusionTerm->atoms[3], EXT_SET) && Narsese_copulaEquals(nar, conclusionTerm->atoms[4], EXT_SET))
        {
            return true;
        }
        //intensional intersection between intensional sets
        if(Narsese_copulaEquals(nar, conclusionTerm->atoms[2], INT_INTERSECTION) && Narsese_copulaEquals(nar, conclusionTerm->atoms[5], INT_SET) && Narsese_copulaEquals(nar, conclusionTerm->atoms[6], INT_SET))
        {
            return true;
        }
    }
    return false;
}

static bool NAL_IndepOrDepVariableAppearsOnce(NAR_t *nar, Term *conclusionTerm)
{
    for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
    {
        if(Variable_isIndependentVariable(nar, conclusionTerm->atoms[i]) || Variable_isDependentVariable(nar, conclusionTerm->atoms[i]))
        {
            for(int j=0; j<COMPOUND_TERM_SIZE_MAX; j++)
            {
                if(i != j && conclusionTerm->atoms[i] == conclusionTerm->atoms[j])
                {
                    goto CONTINUE;
                }
            }
            return true; //not appeared twice
        }
        CONTINUE:;
    }
    return false;
}

static bool NAL_DeclarativeImplicationWithoutIndependentVar(NAR_t *nar, Term *conclusionTerm)
{
    if(Narsese_copulaEquals(nar, conclusionTerm->atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, conclusionTerm->atoms[0], EQUIVALENCE))
    {
        if(!Variable_hasVariable(nar, conclusionTerm, true, false, false))
        {
            return true;
        }
    }
    return false;
}

static bool DeclarativeImplicationWithLefthandConjunctionWithLefthandOperation(NAR_t *nar, Term *conclusionTerm, bool recurse)
{
    //0    1     2    3
    //1    2     3    4
    //==>  &&         ^op
    if(Narsese_copulaEquals(nar, conclusionTerm->atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, conclusionTerm->atoms[0], EQUIVALENCE))
    {
        if(Narsese_copulaEquals(nar, conclusionTerm->atoms[1], CONJUNCTION))
        {
            Term sequence = Term_ExtractSubterm(conclusionTerm, 1);
            return DeclarativeImplicationWithLefthandConjunctionWithLefthandOperation(nar, &sequence, true);
        }
    }
    if(recurse && Narsese_copulaEquals(nar, conclusionTerm->atoms[0], CONJUNCTION))
    {
        Term op_or_sequence = Term_ExtractSubterm(conclusionTerm, 1);
        if(Narsese_isOperation(nar, &op_or_sequence))
        {
            return true;
        }
        else
        if(Narsese_copulaEquals(nar, op_or_sequence.atoms[0], CONJUNCTION))
        {
            return DeclarativeImplicationWithLefthandConjunctionWithLefthandOperation(nar, &op_or_sequence, true);
        }
    }
    return false;
}

void NAL_DerivedEvent(NAR_t *nar, Term conclusionTerm, long conclusionOccurrence, Truth conclusionTruth, Stamp stamp, long currentTime, double parentPriority, double conceptPriority, double occurrenceTimeOffset, Concept *validation_concept, long validation_cid, bool varIntro, bool allowOnlyExtVarIntroAndTwoIndependentVars, bool eternalize)
{
    if(varIntro && (Narsese_copulaEquals(nar, conclusionTerm.atoms[0], TEMPORAL_IMPLICATION) || Narsese_copulaEquals(nar, conclusionTerm.atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, conclusionTerm.atoms[0], EQUIVALENCE)))
    {
        bool success;
        Term conclusionTermWithVarExt = Variable_IntroduceImplicationVariables(nar, conclusionTerm, &success, true);
        if(success && !Term_Equal(&conclusionTermWithVarExt, &conclusionTerm) && !NAL_HOLStatementComponentHasInvalidInhOrSim(nar, &conclusionTermWithVarExt, true) && !NAL_DeclarativeImplicationWithoutIndependentVar(nar, &conclusionTermWithVarExt))
        {
            bool HasTwoIndependentVars = false;
            Atom DepVar2 = Narsese_AtomicTermIndex(nar, "$2");
            for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
            {
                if(conclusionTermWithVarExt.atoms[i] == DepVar2)
                {
                    HasTwoIndependentVars = true;
                    break;
                }
            }
            if(!allowOnlyExtVarIntroAndTwoIndependentVars || HasTwoIndependentVars)
            {
                if(ALLOW_VAR_INTRO)
                {
                    NAL_DerivedEvent(nar, conclusionTermWithVarExt, conclusionOccurrence, conclusionTruth, stamp, currentTime, parentPriority, conceptPriority, occurrenceTimeOffset, validation_concept, validation_cid, false, false, eternalize);
                }
            }
        }
        if(!allowOnlyExtVarIntroAndTwoIndependentVars) //todo rename var to something else
        {
            bool success2;
            Term conclusionTermWithVarInt = Variable_IntroduceImplicationVariables(nar, conclusionTerm, &success2, false);
            if(success2 && !Term_Equal(&conclusionTermWithVarInt, &conclusionTerm) && !NAL_HOLStatementComponentHasInvalidInhOrSim(nar, &conclusionTermWithVarInt, true) && !NAL_DeclarativeImplicationWithoutIndependentVar(nar, &conclusionTermWithVarInt))
            {
                if(ALLOW_VAR_INTRO)
                {
                    NAL_DerivedEvent(nar, conclusionTermWithVarInt, conclusionOccurrence, conclusionTruth, stamp, currentTime, parentPriority, conceptPriority, occurrenceTimeOffset, validation_concept, validation_cid, false, false, eternalize);
                }
            }
        }
        if(Narsese_copulaEquals(nar, conclusionTerm.atoms[0], IMPLICATION) || Narsese_copulaEquals(nar, conclusionTerm.atoms[0], EQUIVALENCE) || allowOnlyExtVarIntroAndTwoIndependentVars)
        {
            return;
        }
    }
    if(varIntro && Narsese_copulaEquals(nar, conclusionTerm.atoms[0], CONJUNCTION))
    {
        bool success;
        Term conclusionTermWithVarExt = Variable_IntroduceConjunctionVariables(nar, conclusionTerm, &success, true);
        if(success && !Term_Equal(&conclusionTermWithVarExt, &conclusionTerm) && !NAL_HOLStatementComponentHasInvalidInhOrSim(nar, &conclusionTermWithVarExt, true))
        {
            if(ALLOW_VAR_INTRO)
            {
                NAL_DerivedEvent(nar, conclusionTermWithVarExt, conclusionOccurrence, conclusionTruth, stamp, currentTime, parentPriority, conceptPriority, occurrenceTimeOffset, validation_concept, validation_cid, false, false, eternalize);
            }
        }
        bool success2;
        Term conclusionTermWithVarInt = Variable_IntroduceConjunctionVariables(nar, conclusionTerm, &success2, false);
        if(success2 && !Term_Equal(&conclusionTermWithVarInt, &conclusionTerm) && !NAL_HOLStatementComponentHasInvalidInhOrSim(nar, &conclusionTermWithVarInt, true))
        {
            if(ALLOW_VAR_INTRO)
            {
                NAL_DerivedEvent(nar, conclusionTermWithVarInt, conclusionOccurrence, conclusionTruth, stamp, currentTime, parentPriority, conceptPriority, occurrenceTimeOffset, validation_concept, validation_cid, false, false, eternalize);
            }
        }
        return;
    }
    Event e = { .term = conclusionTerm,
                .type = EVENT_TYPE_BELIEF,
                .truth = conclusionTruth,
                .stamp = stamp,
                .occurrenceTime = conclusionOccurrence,
                .occurrenceTimeOffset = occurrenceTimeOffset,
                .creationTime = currentTime };
    #pragma omp critical(Memory)
    {
        if(validation_concept == NULL || validation_concept->id == validation_cid) //concept recycling would invalidate the derivation (allows to lock only adding results to memory)
        {
            if(!NAL_AtomAppearsTwice(nar, &conclusionTerm) && !NAL_NestedHOLStatement(nar, &conclusionTerm) && !NAL_InhOrSimHasDepVar(nar, &conclusionTerm) && !NAL_JunctionNotRightNested(nar, &conclusionTerm) && !InvalidSetOp(nar, &conclusionTerm, conclusionTruth) && !NAL_IndepOrDepVariableAppearsOnce(nar, &conclusionTerm) && !DeclarativeImplicationWithLefthandConjunctionWithLefthandOperation(nar, &conclusionTerm, false))
            {
                Memory_AddEvent(nar, &e, currentTime, conceptPriority*parentPriority*Truth_Expectation(conclusionTruth), false, true, false, 0, eternalize);
            }
        }
    }
}
