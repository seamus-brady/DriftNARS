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

void Narsese_Test(NAR_t *nar)
{
    NAR_INIT(nar);
    puts(">>Narsese test start");
    char* narsese = "<<$sth --> (&,[furry,meowing],animal)> =/> <$sth --> [good]>>";
    printf("Narsese: %s\n", narsese);
    char* preprocessed = Narsese_Expand(nar, narsese);
    printf("Preprocessed: %s\n", preprocessed);
    char **tokens = Narsese_PrefixTransform(nar, preprocessed);
    int k = 0;
    for(;tokens[k] != NULL;k++)
    {
        printf("token: %s\n", tokens[k]);
    }
    Term ret = Narsese_Term(nar, narsese);
    for(int i=0; i<COMPOUND_TERM_SIZE_MAX; i++)
    {
        if(ret.atoms[i] != 0)
        {
            printf("Subterm: %i %d %s\n", i, ret.atoms[i], nar->atom_names[ret.atoms[i]-1]);
        }
    }
    puts("Result:");
    Narsese_PrintTerm(nar, &ret);
    puts("");
    /* Test oversized input doesn't crash */
    char oversized[NARSESE_LEN_MAX + 100];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    char *expanded = Narsese_Expand(nar, oversized);
    assert(expanded == NULL, "Oversized Narsese input should return NULL");

    /* Verify normal parsing still works after overflow attempt */
    Term t_after = Narsese_Term(nar, "<cat --> animal>");
    assert(t_after.atoms[0] != 0, "Parsing should still work after overflow attempt");

    puts(">>Narsese Test successful");
    Narsese_PrintTerm(nar, &ret);
    puts("");
}
