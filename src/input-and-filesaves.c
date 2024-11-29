
#include "editor.h"

/* --------------------------------------- user input --------------------------------------- */

// For commands, searching, saving: opens a prompt at the bottom line of editor
char* editor_prompt(char* prompt, void (*callback)(char*, int)){
    size_t bufsize = 128;
    char* buf = malloc(bufsize); // 1 byte per char
    size_t buflen = 0;
    buf[0] = '\0'; // empty str

    // infinite loop that only changes the status bar
    while (1) {
        editor_set_status_msg(prompt, buf);
        editor_refresh_screen();

        int c = editor_read_key();

        if(c == BACKSPACE){
            //backspace
            if(buflen != 0) buf[--buflen] = '\0';
        }else if(c == '\x1b' || c == CTRL_KEY('c')){
            // exit the prompt by pressing <esc>
            editor_set_status_msg("");
            if(callback) callback(buf, c);
            free(buf);
            return NULL;
        }else if (c == '\r') {
            // <enter> has been pressed
            // only exit when a response has been typed
            if (buflen != 0) {
                editor_set_status_msg("");
                if(callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && (int)c < 128) {
            // check that c is a valid character and non-control

            if (buflen == bufsize - 1) { // out of bounds
                // make more space
                bufsize *= 2; // common practice in vectors
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0'; // continously end with \0
        }
        if(callback) callback(buf, c);
    }
}

// Moving cursor on non-insertion/deletion of characters. Purely movement characters.
void move_cursor(int c){
    erow* row = (state.cy >= state.num_rows) ? NULL : &state.row[state.cy];
    if(!row) error("moving on non-row"); // I have it setup so that row should never be NULL
    switch (c) {
        case ARROW_LEFT: // h
            if(state.cx != 0){
                --state.cx;
            }else if(state.cy > 0){
                --state.cy;
                state.cx = state.row[state.cy].size;
            }
            break;
        case ARROW_DOWN: // j
            // num rows is the amount of rows in the current file being viewed
            // so if there is no file, we cannot move down
            if(state.cy < (state.num_rows-1)){
                ++state.cy;
            }
            break;
        case ARROW_UP: // k
            if(state.cy != 0){
                --state.cy;
            }
            break;
        case ARROW_RIGHT: // l
            if(state.cx < row->size){
                ++state.cx;
            }else if(state.cy < (state.num_rows-1)){
                ++state.cy;
                state.cx = 0;
            }
            break;
    }
    // snap the horizontal to the end of each line
    row = (state.cy >= state.num_rows) ? NULL : &state.row[state.cy];
    if(!row) error("moving on non-row");

    int rowlen = row ? row->size : 0;
    if (state.cx > rowlen) {
        state.cx = rowlen;
    }
}

// Read keys, made for handling special defined keys, and multi-byte escape sequences that can be read due to changing vmin and vtime
int editor_read_key() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) error("read");
    }
    // check for escape characters
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

// read 1 byte from STDIN, store in address of int c (char). Handles all keybind specifications.
void editor_keypress_handler(){
    static int quit_times = QUIT_TIMES;

    int c = editor_read_key();

    switch(c){
        case CTRL_KEY('c'):
            if(state.dirty && quit_times > 0){
                editor_set_status_msg("WARNING!!! File has unsaved changes, press CTRL-c %d more times to quit without saving.", quit_times);
                --quit_times;
                return;
            }
            exit(0);
            break;
        case CTRL_KEY('d'):
            if(state.cy < state.num_rows - 10){
                state.cy += 10;
            }else{
                state.cy = state.num_rows - 1;
            }
            break;
        case CTRL_KEY('u'):
            if(state.cy >= 10){
                state.cy -= 10;
            }else{
                state.cy = 0;
            }
            break;
        case CTRL_KEY('s'):
            editor_save();
            break;
        case '0':
            state.cx = 0;
            break;
        case '$':
            if(state.cy < state.num_rows){
                state.cx = state.row[state.cy].size;
            }
            break;
        case 'G':
            state.cy = state.num_rows - 1;
            break;
        case CTRL_KEY('f'):
            editor_find();
            break;
        case BACKSPACE: // backspace
            editor_delete_char();
            break;
        case '\r': // enter
            editor_insert_newline();
            break;
        case '\t': // tab
            editor_insert_char(c);
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            move_cursor(c);
            break;
        default:
            if(!iscntrl(c)){
                editor_insert_char(c);
            }
            break;
    }

    // reset quit times after processing other inputs
    quit_times = QUIT_TIMES;
}

/* ------------------------------------ file input/output ------------------------------------ */
// expects the caller to clear the memory of returned char*.
// Used for saving contents of file to disk.
char* editor_rows_to_string(int* buflen){
    int total_len = 0;
    int i;
    // +1 for newline characters to be added and the final null byte
    for(i=0; i<state.num_rows; ++i){
        total_len += state.row[i].size + 1;
    }
    *buflen = total_len;

    char* buf = malloc(total_len);

    // use another pointer as a walker through the buf memory
    char* p = buf;
    for(i = 0; i<state.num_rows; ++i){
        memcpy(p, state.row[i].chars, state.row[i].size);
        p += state.row[i].size;
        *p = '\n';
        p++;
    }
    return buf;
}

// Opens file, parses file lines, fills editor state with line contents (erows).
void editor_open(char* filename){
    free(state.filename);
    int len = strlen(filename);
    state.filename = (char*) malloc(len + 1);
    strcpy(state.filename, filename); // includes null-byte

    FILE* fp = fopen(filename, "r");
    if (!fp) error("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t length;
    // using getline() because we don't know how much memory to allocate per line
    while((length = getline(&line, &linecap, fp)) != -1){
        if (length == -1) error("getline");

        // get rid of any trailing whitespace by reducing length
        while(length > 0 &&
             (line[length-1] == '\n' ||
              line[length-1]=='\r')){
            --length;
        }

        editor_insert_row(state.num_rows, line, length);
    }

    free(line);
    fclose(fp);
}

// Save file, if file doesn't exist, prompts for new file creation
void editor_save(){
    if(state.filename == NULL){
        state.filename = editor_prompt("Save as: %s", NULL);
        if(state.filename == NULL){
            editor_set_status_msg("Save cancelled");
            return;
        }
    }

    int len;
    char *buf = editor_rows_to_string(&len);
    // 0644 are standard permissions for text files
    int fd = open(state.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                editor_set_status_msg("%d bytes written to disk", len);
                state.dirty = 0;
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editor_set_status_msg("Failed to save. Error: %s", strerror(errno));
}


