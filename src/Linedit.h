#ifndef H_LINEDIT
#define H_LINEDIT

///////////////
//  Linedit  //
///////////////
// Minimal raw-mode line editor for interactive terminals.
// Falls back to plain fgets() when stdin is not a TTY.

// Read one line with the given prompt. Returns pointer to a static buffer,
// or NULL on EOF. The caller must not free the returned pointer.
char *Linedit_Read(const char *prompt);

// Restore terminal to cooked mode (safe to call even if never entered raw mode).
void Linedit_Cleanup(void);

#endif
