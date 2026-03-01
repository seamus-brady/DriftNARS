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

void InvertedAtomIndex_Test(NAR_t *nar)
{
    puts(">>Inverted atom index test start");
    NAR_INIT(nar);
    Term term = Narsese_Term(nar, "<a --> (b & c)>");
    Concept c = { .term = term };
    InvertedAtomIndex_AddConcept(nar, term, &c);
    InvertedAtomIndex_Print(nar);
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "a")]->c == &c, "There was no concept reference added for key a!");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "b")]->c == &c, "There was no concept reference added for key b!");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "c")]->c == &c, "There was no concept reference added for key c!");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, ":")] == NULL, "There was a concept reference added for key inheritance!");
    InvertedAtomIndex_RemoveConcept(nar, term, &c);
    InvertedAtomIndex_Print(nar);
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "a")] == NULL, "Concept reference was not removed for key a!");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "b")] == NULL, "Concept reference was not removed for key b!");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "c")] == NULL, "Concept reference was not removed for key c!");
    InvertedAtomIndex_AddConcept(nar, term, &c);
    Term term2 = Narsese_Term(nar, "<b --> d>");
    Concept c2 = { .term = term2 };
    InvertedAtomIndex_AddConcept(nar, term2, &c2);
    InvertedAtomIndex_Print(nar);
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "a")]->c == &c, "There was no concept reference added for key a! (2)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "b")]->c == &c, "There was no concept reference added for key b! (2)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "c")]->c == &c, "There was no concept reference added for key c! (2)");
    assert(((ConceptChainElement*) nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "b")]->next)->c == &c2, "There was no concept2 reference added for key b! (2)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "d")]->c == &c2, "There was no concept2 reference added for key d! (2)");
    InvertedAtomIndex_RemoveConcept(nar, term, &c);
    puts("after removal");
    InvertedAtomIndex_Print(nar);
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "b")]->c == &c2, "There was no concept2 reference remaining for key b! (3)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "d")]->c == &c2, "There was no concept2 reference remaining for key d! (3)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "a")] == NULL, "Concept reference was not removed for key a! (3)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "c")] == NULL, "Concept reference was not removed for key c! (3)");
    InvertedAtomIndex_RemoveConcept(nar, term2, &c2);
    puts("after removal2");
    InvertedAtomIndex_Print(nar);
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "a")] == NULL, "Concept reference was not removed for key a! (4)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "b")] == NULL, "Concept reference was not removed for key b! (4)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "c")] == NULL, "Concept reference was not removed for key c! (4)");
    assert(nar->inverted_atom_index[Narsese_AtomicTermIndex(nar, "d")] == NULL, "Concept reference was not removed for key d! (4)");
    puts(">>Inverted atom index test successful");
}
