
#include "editor.h"

/* --------------------------------------- append buffer --------------------------------------- */
// For appending a string to the end of the current append buffer
void ab_append(struct abuf* ab, const char* s, int len){
    // allocate for the new string
    char* new = realloc(ab->b, ab->len + len);
    if(new == NULL) return;

    // copy new string s into new[.], strcpy would also work
    // memcpy because len is actually size in bytes, and we don't want to just be copying characters
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

// deallocate the malloced char array in append buffer
void ab_free(struct abuf* ab){
    free(ab->b);
}

/* --------------------------------------- drawing to screen --------------------------------------- */

// Scrolls when file is larger than screen, when cursor is off, or trying to move off the screen
void editor_scroll() {
    state.rx = 0;
    if (state.cy < state.num_rows) {
        state.rx = editor_row_cx_to_rx(state.row + state.cy, state.cx);
    }

    // Vertical -----
    // Since cy is relative to the file, we need to subtract
    // the current file row offset to see if it is off-screen
    if ((state.cy - state.rowoff) < 0) {
        state.rowoff = state.cy;
    }

    // once the cursor position is off the screen of rows,
    // the rowoff will increment (but since we might have jumped
    // several lines down, we should calculate the new offset).
    if ((state.cy - state.rowoff) >= state.screen_rows) {
        state.rowoff = state.cy - state.screen_rows + 1;
    }

    // Horizontal -----
    if((state.rx - state.coloff) < 0){
        state.coloff = 0;
    }
    if((state.rx - state.coloff) >= state.screen_cols){
        state.coloff = state.rx - state.screen_cols + 1;
    }
}

// write all the contents of the append buffer to the terminal
void editor_draw_rows(struct abuf* ab){
    int i;
    for(i=0;i<state.screen_rows; ++i){
        int filerow = i + state.rowoff;
        if(filerow >= state.num_rows){
            // check if we are outside the range of the currently edited number of rows
            ab_append(ab, "~", 1);
        }else{
            int len = state.row[filerow].rsize - state.coloff;
            if(len < 0) len = 0;
            if(len > state.screen_cols) len = state.screen_cols;

            // move through the portion of the row that should be displayed on screen
            char* p = state.row[filerow].render + state.coloff; // render array ptr
            uint8_t* highlights = state.row[filerow].hl + state.coloff; // hl array ptr

            // only change color when it is different from the previous character
            int curr_color = -1;

            int i;
            for(i=0;i<len;++i){
                if(iscntrl(p[i])){ // for non-printable characters, make them print nicely
                    char symbol = (p[i] <= 26) ? '@' + p[i] : '?';
                    ab_append(ab, "\x1b[7m", 4); // invert colors
                    ab_append(ab, &symbol, 1);
                    ab_append(ab, "\x1b[m", 3); // revert colors
                    if (curr_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", curr_color);
                        ab_append(ab, buf, clen); // add current color back
                    }
                }else if(highlights[i] == HL_NORMAL){   // making the common case fast
                    if(curr_color != -1){
                        curr_color = -1;
                        ab_append(ab, "\x1b[39m", 5); // default color
                    }
                    ab_append(ab, p + i, 1);
                }else{
                    int h_color = editor_syntax_to_color(highlights[i]);
                    if(curr_color != h_color){
                        curr_color = h_color;

                        char sequence_buf[16];
                        int sequence_length = snprintf(sequence_buf, sizeof(sequence_buf), "\x1b[%dm", h_color);
                        ab_append(ab, sequence_buf, sequence_length); // update color
                    }
                    ab_append(ab, p + i, 1);
                }
            }
            ab_append(ab, "\x1b[39m", 5); // reset to default color
        }
        ab_append(ab, "\x1b[K", 3); // erase to the right of current line
        ab_append(ab, "\r\n", 2); // dont do newline at bottom
    }
}

// write a status bar to the last line of the terminal
void editor_draw_status_bar(struct abuf* ab){
    ab_append(ab, "\x1b[7m", 4); // invert colors escape sequence
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
            state.filename ? state.filename : "[No Name]", state.num_rows, state.dirty ? "[+]" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
            state.cy + 1, state.num_rows);
    if (len > state.screen_cols) len = state.screen_cols;
    ab_append(ab, status, len);
    for(;len<state.screen_cols; ++len){
        if(state.screen_cols - len == rlen){
            ab_append(ab, rstatus, rlen);
            break;
        }
        ab_append(ab, " ", 1); // color the bar
    }
    ab_append(ab, "\x1b[m", 3); // undo color invert
    ab_append(ab, "\r\n", 2);
}

// write a content to the space for where messages and prompts come up
void editor_draw_msg_bar(struct abuf* ab){
    ab_append(ab, "\x1b[K", 3);
    int msg_len = strlen(state.statusmsg);
    if (msg_len > state.screen_cols) msg_len = state.screen_cols;
    
    // Only write it if it is 5 seconds old
    // Note that it will stay on the screen until editor_refresh_screen() is called
    // which is on user input, which is good.
    if (msg_len && (time(NULL) - state.statusmsg_time < 5))
        ab_append(ab, state.statusmsg, msg_len);
}

// uses variable argument number, from stdarg.h
// This is for passing arguments like: msg("%d bytes", len);
void editor_set_status_msg(const char* fmt, ...){
    // variable list of arguments
    va_list ap; // arg pointer
    va_start(ap, fmt); // bring pointer to the start of the list of args

   // stores in statusmsg what the formatted output would normally be if the
   // arguments were used in printf
    vsnprintf(state.statusmsg, sizeof(state.statusmsg), fmt, ap);
    va_end(ap); // free
    state.statusmsg_time = time(NULL);
}

// update screen by drawing all contents (occurs on any keypress)
void editor_refresh_screen(){
    editor_scroll();

    struct abuf ab;
    ab.b = NULL;
    ab.len = 0;

    ab_append(&ab, "\x1b[?25l", 6); // hide cursor
    ab_append(&ab, "\x1b[H", 3); // reposition cursor to top of screen

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_msg_bar(&ab);

    // the terminal uses 1-indexing, so (1,1) is the top left corner
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (state.cy - state.rowoff) + 1, (state.rx - state.coloff) + 1);
    // append this escape sequence, so that we move cursor to the designated location
    // Note that this is moving the cursor to a position relative to the screen, not the file
    ab_append(&ab, buf, strlen(buf));


    ab_append(&ab, "\x1b[?25h", 6); // show cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}
