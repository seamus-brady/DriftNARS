#include "Linedit.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>

#define LINEDIT_BUFSZ  1024
#define LINEDIT_HISTSZ 32

static char buf[LINEDIT_BUFSZ];
static char history[LINEDIT_HISTSZ][LINEDIT_BUFSZ];
static int  hist_count = 0;
static int  hist_write = 0;  // next slot to write into (ring)

static struct termios orig_termios;
static bool  termios_saved = false;

static void raw_enable(void)
{
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    termios_saved = true;
    raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void raw_disable(void)
{
    if(termios_saved)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }
}

static void hist_add(const char *line)
{
    if(line[0] == '\0') return;
    // don't add duplicates of the most recent entry
    if(hist_count > 0)
    {
        int prev = (hist_write - 1 + LINEDIT_HISTSZ) % LINEDIT_HISTSZ;
        if(strcmp(history[prev], line) == 0) return;
    }
    strncpy(history[hist_write], line, LINEDIT_BUFSZ - 1);
    history[hist_write][LINEDIT_BUFSZ - 1] = '\0';
    hist_write = (hist_write + 1) % LINEDIT_HISTSZ;
    if(hist_count < LINEDIT_HISTSZ) hist_count++;
}

static void refresh_line(const char *prompt, const char *line, int len, int pos)
{
    // move cursor to column 0, clear line, write prompt + buffer, reposition cursor
    fprintf(stdout, "\r\033[K%s%s", prompt, line);
    // move cursor back to the correct position
    int back = len - pos;
    if(back > 0) fprintf(stdout, "\033[%dD", back);
    fflush(stdout);
}

char *Linedit_Read(const char *prompt)
{
    // non-TTY: fall back to plain fgets, no prompt
    if(!isatty(STDIN_FILENO))
    {
        if(fgets(buf, LINEDIT_BUFSZ, stdin) == NULL) return NULL;
        // strip trailing newline
        int len = (int)strlen(buf);
        if(len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        return buf;
    }

    raw_enable();

    int len = 0;
    int pos = 0;
    int hist_idx = -1;  // -1 means "current input", 0..hist_count-1 are history entries
    char saved[LINEDIT_BUFSZ] = {0};  // saves current input when browsing history
    buf[0] = '\0';

    fprintf(stdout, "%s", prompt);
    fflush(stdout);

    for(;;)
    {
        char c;
        if(read(STDIN_FILENO, &c, 1) != 1)
        {
            // EOF
            raw_disable();
            return NULL;
        }

        if(c == '\r' || c == '\n')
        {
            // accept line
            buf[len] = '\0';
            fputs("\r\n", stdout);
            fflush(stdout);
            raw_disable();
            hist_add(buf);
            return buf;
        }
        else if(c == 4) // Ctrl-D
        {
            if(len == 0)
            {
                fputs("\r\n", stdout);
                fflush(stdout);
                raw_disable();
                return NULL;
            }
        }
        else if(c == 3) // Ctrl-C: clear line
        {
            len = 0;
            pos = 0;
            buf[0] = '\0';
            hist_idx = -1;
            refresh_line(prompt, buf, len, pos);
        }
        else if(c == 127 || c == 8) // backspace
        {
            if(pos > 0)
            {
                memmove(buf + pos - 1, buf + pos, len - pos);
                pos--;
                len--;
                buf[len] = '\0';
                refresh_line(prompt, buf, len, pos);
            }
        }
        else if(c == 1) // Ctrl-A / Home
        {
            pos = 0;
            refresh_line(prompt, buf, len, pos);
        }
        else if(c == 5) // Ctrl-E / End
        {
            pos = len;
            refresh_line(prompt, buf, len, pos);
        }
        else if(c == 27) // escape sequence
        {
            char seq[2];
            if(read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if(read(STDIN_FILENO, &seq[1], 1) != 1) continue;

            if(seq[0] == '[')
            {
                if(seq[1] == 'D') // left arrow
                {
                    if(pos > 0)
                    {
                        pos--;
                        refresh_line(prompt, buf, len, pos);
                    }
                }
                else if(seq[1] == 'C') // right arrow
                {
                    if(pos < len)
                    {
                        pos++;
                        refresh_line(prompt, buf, len, pos);
                    }
                }
                else if(seq[1] == 'A') // up arrow: older history
                {
                    if(hist_count > 0)
                    {
                        if(hist_idx == -1)
                        {
                            // save current input
                            memcpy(saved, buf, LINEDIT_BUFSZ);
                            hist_idx = 0;
                        }
                        else if(hist_idx < hist_count - 1)
                        {
                            hist_idx++;
                        }
                        else
                        {
                            continue;
                        }
                        // history is stored newest-first: index 0 = most recent
                        int slot = (hist_write - 1 - hist_idx + LINEDIT_HISTSZ) % LINEDIT_HISTSZ;
                        strncpy(buf, history[slot], LINEDIT_BUFSZ - 1);
                        buf[LINEDIT_BUFSZ - 1] = '\0';
                        len = (int)strlen(buf);
                        pos = len;
                        refresh_line(prompt, buf, len, pos);
                    }
                }
                else if(seq[1] == 'B') // down arrow: newer history
                {
                    if(hist_idx > 0)
                    {
                        hist_idx--;
                        int slot = (hist_write - 1 - hist_idx + LINEDIT_HISTSZ) % LINEDIT_HISTSZ;
                        strncpy(buf, history[slot], LINEDIT_BUFSZ - 1);
                        buf[LINEDIT_BUFSZ - 1] = '\0';
                        len = (int)strlen(buf);
                        pos = len;
                        refresh_line(prompt, buf, len, pos);
                    }
                    else if(hist_idx == 0)
                    {
                        hist_idx = -1;
                        memcpy(buf, saved, LINEDIT_BUFSZ);
                        len = (int)strlen(buf);
                        pos = len;
                        refresh_line(prompt, buf, len, pos);
                    }
                }
                else if(seq[1] == 'H') // Home
                {
                    pos = 0;
                    refresh_line(prompt, buf, len, pos);
                }
                else if(seq[1] == 'F') // End
                {
                    pos = len;
                    refresh_line(prompt, buf, len, pos);
                }
            }
        }
        else if(c >= 32) // printable character
        {
            if(len < LINEDIT_BUFSZ - 1)
            {
                memmove(buf + pos + 1, buf + pos, len - pos);
                buf[pos] = c;
                pos++;
                len++;
                buf[len] = '\0';
                refresh_line(prompt, buf, len, pos);
            }
        }
    }
}

void Linedit_Cleanup(void)
{
    raw_disable();
}
