#ifndef H_DRIFTSCRIPT
#define H_DRIFTSCRIPT

#define DS_RESULTS_MAX 256
#define DS_OUTPUT_MAX  4096

typedef enum {
    DS_RES_NARSESE,
    DS_RES_SHELL_COMMAND,
    DS_RES_CYCLES,
    DS_RES_DEF_OP
} DS_ResultKind;

typedef struct {
    DS_ResultKind kind;
    char value[DS_OUTPUT_MAX];
} DS_CompileResult;

int DS_CompileSource(const char *source, DS_CompileResult *results, int max_results);
const char *DS_GetError(void);

#endif
