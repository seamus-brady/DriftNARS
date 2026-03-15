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

#include "InvertedAtomIndex.h"
#include "NAR.h"

void InvertedAtomIndex_INIT(NAR_t *nar)
{
    for(int i=0; i<ATOMS_MAX; i++)
    {
        nar->inverted_atom_index[i] = NULL;
    }
    Stack_INIT(&nar->cce_stack, (void**) nar->cce_storageptrs, UNIFICATION_DEPTH*CONCEPTS_MAX);
    for(int i=0; i<UNIFICATION_DEPTH*CONCEPTS_MAX; i++)
    {
        nar->cce_storage[i] = (ConceptChainElement) {0};
        nar->cce_storageptrs[i] = NULL;
        Stack_Push(&nar->cce_stack, &nar->cce_storage[i]);
    }
}

void InvertedAtomIndex_AddConcept(NAR_t *nar, Term term, Concept *c)
{
    for(int i=0; i<UNIFICATION_DEPTH; i++)
    {
        Atom atom = term.atoms[i];
        if(Narsese_IsSimpleAtom(nar, atom))
        {
            ConceptChainElement *elem = nar->inverted_atom_index[atom];
            if(elem == NULL)
            {
                ConceptChainElement *newElem = Stack_Pop(&nar->cce_stack); //new item
                if(newElem == NULL) goto NEXT_ATOM; //pool exhausted
                newElem->c = c;
                nar->inverted_atom_index[atom] = newElem;
            }
            else
            {
                //search for c:
                ConceptChainElement *previous = NULL;
                while(elem != NULL)
                {
                    if(elem->c == c)
                    {
                        goto NEXT_ATOM;
                    }
                    previous = elem;
                    elem = elem->next;
                }
                //ok, we can add it as previous->next
                ConceptChainElement *newElem = Stack_Pop(&nar->cce_stack); //new item
                if(newElem == NULL) goto NEXT_ATOM; //pool exhausted
                newElem->c = c;
                previous->next = newElem;
            }
        }
        NEXT_ATOM:;
    }
}

void InvertedAtomIndex_RemoveConcept(NAR_t *nar, Term term, Concept *c)
{
    for(int i=0; i<UNIFICATION_DEPTH; i++)
    {
        Atom atom = term.atoms[i];
        if(Narsese_IsSimpleAtom(nar, atom))
        {
            ConceptChainElement *previous = NULL;
            ConceptChainElement *elem = nar->inverted_atom_index[atom];
            while(elem != NULL)
            {
                if(elem->c == c) //we found c in the chain, remove it
                {
                    if(previous == NULL) //item was the initial chain element, let the next element be the initial now
                    {
                        nar->inverted_atom_index[atom] = elem->next;
                    }
                    else //item was within the chain, relink the previous to the next of elem
                    {
                        previous->next = elem->next;
                    }
                    //push elem back to the stack, it's "freed"
                    assert(elem->c != NULL, "A null concept was in inverted atom index!");
                    elem->c = NULL;
                    elem->next = NULL;
                    Stack_Push(&nar->cce_stack, elem);
                    goto NEXT_ATOM;
                }
                previous = elem;
                elem = elem->next;
            }
        }
        NEXT_ATOM:;
    }
}

void InvertedAtomIndex_Print(NAR_t *nar)
{
    puts("printing inverted atom table content:");
    for(int i=0; i<ATOMS_MAX; i++)
    {
        Atom atom = i; //the atom is directly the value (from 0 to ATOMS_MAX)
        if(Narsese_IsSimpleAtom(nar, atom))
        {
            ConceptChainElement *elem = nar->inverted_atom_index[atom];
            while(elem != NULL)
            {
                Concept *c = elem->c;
                assert(c != NULL, "A null concept was in inverted atom index!");
                Narsese_PrintAtom(nar, atom);
                fputs(" -> ", stdout);
                Narsese_PrintTerm(nar, &c->term);
                puts("");
                elem = elem->next;
            }
        }
    }
    puts("table print finish");
}

ConceptChainElement* InvertedAtomIndex_GetConceptChain(NAR_t *nar, Atom atom)
{
    ConceptChainElement* ret = NULL;
    if(atom != 0)
    {
        ret = nar->inverted_atom_index[atom];
    }
    return ret;
}
