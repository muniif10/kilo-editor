#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include <asm-generic/ioctls.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

/*** Data ***/

typedef struct erow {
    int size;
    char *chars;
    int rsize;
    char *render;
}erow;


struct editorConfig
{
    int screenrows;
    int screencols;
    struct termios orig_termios;
    int numrows;
    int rowoff;
    int coloff;
    erow  *row;
    int cx, cy;
    int rx;
};

struct editorConfig E;
/** Definitions **/


#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    DEL_KEY

};

/*** Terminal ***/
struct termios orig_termios;

void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 4);
    perror(s);
    exit(1);
}
int editorReadKey()
{
    int nread;
    char c;
    while (
        (nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {

            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }
            else
        {
                switch (seq[1])
                {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O'){
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;

            }
        }
        return '\x1b';
    }
    else
{
        return c;
    }
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    unsigned int i = 0;
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    }
    else
{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
    // Define new to be holding char
    // points the new variable to whatever is returned by realloc
    // realloc takes the pointer to the allocated memory and a new requested memory size and decide to reallocate or expand the current pointer and return that pointer back.
    char *new = realloc(ab->b, ab->len + len);
    // checks if we get a pointer or not
    // if not we fail to get a new address
    if (new == NULL)
        return;
    // if got,
    // copy the s content to the new starting
    // at the previous array size until len byte
    // has been copied.
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);
    struct termios raw = E.orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/*** row operations ***/

void editorUpdateRow(erow *row){
    int tabs = 0;
    int j;
    for (j =0; j < row->size; j++){
        if (row->chars[j] == '\t') tabs++;
    }

    free(row->render);
    row->render = malloc(row->size +tabs*(KILO_TAB_STOP -1) +1);

    int idx = 0;
    for (j= 0; j< row->size; j++){
        if (row->chars[j] == '\t'){
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP  != 0) row->render[idx++] = ' ';
        }
        else{
            row->render[idx++] = row->chars[j];
        }
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len){
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    // Store the line length to the global row size
    E.row[at].size = len;
    // Allocate memory for the line plus the terminating char
    E.row[at].chars = malloc(len + 1);
    // Copy the line into global row
    memcpy(E.row[at].chars,s,len);
    // Set the terminating char
    E.row[at].chars[len] = '\0';
    // Increment after appending newly read 
    E.numrows++;

    E.row[at].rsize = 0;
    E.row[at].render = NULL;

    editorUpdateRow(&E.row[at]);
}

/*** file i/o ***/

void editorOpen(char *filename){
    FILE *fp = fopen(filename,"r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    // getline returns how much byte it loaded
    while ((linelen = getline(&line, &linecap, fp)) != -1){
        while (linelen >0 && (line[linelen-1] == '\n' ||
            line[linelen-1] == '\r'))
            linelen--;
        editorAppendRow(line,linelen);
    }
    free(line);
    fclose(fp);
}

/*** Input ***/

void editorMoveCursor(int key)
{
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key)
    {
        case ARROW_LEFT:
            /*E.cx--;*/
            if (E.cx != 0)
            {
                E.cx--;
            } else if(E.cy > 0){
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            // If row is pointing to something,
            // check if the cursor is to the right
            // of the row length
            // we want it at most be one unit to the right
            // of the end of the row for editing purpose.
            if (row && E.cx < row->size){
                E.cx++;
            }else if (row && E.cx == row->size){
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0)
            {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows)
            {
                E.cy++;
            }
            break;
            row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
            int rowlen = row ? row->size : 0;
            if (E.cx > rowlen){
                E.cx = rowlen;
            }
    }
}

void editorProcessKey()
{
    int c = editorReadKey();
    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 4);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screencols -1;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--){
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** Output ***/

void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++){
        int filerow = y + E.rowoff;
        if (filerow >=E.numrows){
            if (E.numrows == 0 && y == E.screenrows / 3){
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                                          "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols)
                    welcomelen = E.screencols;
                // Produce the number of padding space needed
                int padding = (E.screencols - welcomelen) / 2;
                // If padding needed (exist case where padding is 0)
                if (padding)
                {
                    // Add the single tilde
                    abAppend(ab, "~", 1);
                    // Since we added the tiled in place of tilde, decrement it
                    padding--;
                }
                // While padding is not 0, just add padding to the screen buffer
                while (padding--)
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }
            else
        {
                abAppend(ab, "~", 1);
            }
        }
        else{
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len =0;
            if (len > E.screencols
            ) len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        /*write(STDOUT_FILENO,"~",1);*/
        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows - 1)
        {
            /*write(STDOUT_FILENO, "\r\n",2);*/
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorScroll()
{
    if (E.cy < E.rowoff){
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows){
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.cx < E.coloff){
        E.coloff = E.cx;
    }
    if (E.cx > E.coloff + E.screencols){
        E.coloff = E.cx -E.screencols + 1;
    }
}

/*** Init ***/

void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.rx = 0;
    E.row = NULL;
    if (getWindowSize(&E.screenrows, &E.screencols))
        die("getWindowSize");
}

void editorRefreshScreen()
{
    editorScroll();
    struct abuf ab = ABUF_INIT;

    // Hide cursor
    abAppend(&ab, "\x1b[?25l", 6);
    // Position to top left
    abAppend(&ab, "\x1b[H", 3);

    /*write(STDOUT_FILENO, "\x1b[2J",4);*/
    /*write(STDOUT_FILENO, "\x1b[H",3);*/

    editorDrawRows(&ab);

    char buf[32];
    // Add into buffer the escape sequence and the coordinate of the cursor
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) +1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // Position to top left
    /*abAppend(&ab, "\x1b[H",3);*/
    // Show cursor
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
    /*write(STDOUT_FILENO, "\x1b[H",3);*/
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2){
        editorOpen(argv[1]);
    }
    while (1)
    {
        editorRefreshScreen();
        editorProcessKey();
    }

    return 0;
}
