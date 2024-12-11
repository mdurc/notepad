
#include "editor.h"

/* ------------------------------------ row operations ------------------------------------ */
// Example: moving cursor forward TAB_STOP spaces when placed in the middle of a tab.
int editor_row_cx_to_rx(erow* row, int cx){
    int rx = 0;
    int i;
    for(i=0; i<cx; ++i){
        // find how many spaces until the end of the current tabstop, even if we are in the middle
        if(row->chars[i] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        ++rx;
    }
    return rx;
}

// Used for locating the cx when we have the render x-position during searching.
int editor_row_rx_to_cx(erow* row, int rx){
    int curr_rx = 0;
    int cx;
    for(cx = 0; cx < row->size; ++cx){
        if(row->chars[cx] == '\t'){
            curr_rx += (TAB_STOP - 1) - (curr_rx % TAB_STOP);
        }
        ++curr_rx;
        if(curr_rx > rx) return cx;
    }
    return cx; // just in case the parameters were out of bounds
}

// move row->chars into the "render" characters, adjusting for tabs
void editor_update_row(erow* row){
    int tabs = 0;
    int i, j;
    for (j = 0; j < row->size; ++j){
        if (row->chars[j] == '\t') ++tabs;
    }
    free(row->render);
    // tabs will take up a maximum of 8 characters
    // row->size already counts 1, so do tabs*7
    row->render = malloc(row->size + tabs*(TAB_STOP-1) + 1);

    for(i=0, j=0;j<row->size;++j){
        if (row->chars[j] == '\t') {
            row->render[i++] = ' ';
            while (i % TAB_STOP != 0) row->render[i++] = ' ';
        } else {
            row->render[i++] = row->chars[j];
        }
    }
    row->render[i] = '\0';
    row->rsize = i;

    // after updating the render characters, update the syntax that is based on render
    editor_update_syntax(row);
}

// insert a row in position: row_num, with contents: line, and length: len
void editor_insert_row(int row_num, char* line, size_t len){
    if(row_num < 0 || row_num > state.num_rows) return;

    // allocate space for a new row
    state.row = realloc(state.row, sizeof(erow) * (state.num_rows + 1));

    int i;
    // note that state.row now has +1 allocated, so i is not out of bounds
    for(i=state.num_rows; i > row_num; --i){
        state.row[i] = state.row[i-1];
        ++state.row[i].idx; // adjust index
    }

    state.row[row_num].idx = row_num;

    state.row[row_num].size = len;
    state.row[row_num].chars = malloc(len + 1); // room for null char
    memcpy(state.row[row_num].chars, line, len);
    state.row[row_num].chars[len] = '\0';

    state.row[row_num].rsize = 0;
    state.row[row_num].render = NULL;
    state.row[row_num].hl = NULL;
    state.row[row_num].hl_open_comment = 0;
    editor_update_row(&state.row[row_num]); // update the render characters in state

    ++state.num_rows;
}

// freeing memory of a given row, when the row is deleted
void editor_free_row(erow* row){
    if(row){
        if(row->render) free(row->render);
        if(row->chars) free(row->chars);
        if(row->hl) free(row->hl);
    }
}

// when backspace on an empty row
void editor_delete_row(int row_num){
    if (row_num < 0 || row_num >= state.num_rows) return;
    if(!state.undoing) editor_push_undo(&state.row[state.cy], DELETE_ROW);
    // free the memory
    editor_free_row(&state.row[row_num]);

    // remove the row from the array
    int i;
    for(i=row_num; i<state.num_rows-1; ++i){
        // all attributes should be copied,
        // ptrs will be copied and it should be fine
        state.row[i] = state.row[i+1];
        --state.row[i].idx; // update index of the remaining rows
    }
    // clear the ending row
    state.row[i].size = 0;
    state.row[i].rsize = 0;
    state.row[i].chars = NULL;
    state.row[i].render = NULL;
    --state.num_rows;
    state.dirty = 1;
}

// simply inserts character into char array. Doesn't have to worry about where the cursor is
void editor_row_insert_char(erow* row, int column, char c){
    if(column < 0 || column > row->size) column = row->size;
    // add room for new character and null-byte
    row->chars = realloc(row->chars, row->size + 2);

    ++row->size;
    int i;
    // ripple through and carry all chars to the end
    for(i=column; i<row->size; ++i){
        char temp = row->chars[i];
        row->chars[i] = c;
        c = temp;
    }
    row->chars[i] = '\0';
    editor_update_row(row);
}

// backspace on a non-empty line: append the contents of current line to the end of previous line
void editor_row_append_string(erow* row, char* s, size_t len){
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
}

// simply removes character from char array in row
void editor_row_delete_char(erow* row, int column){
    if(column < 0 || column  >= row->size) return;

    int i;
    // ripple through and carry all chars after column back one space
    for(i=column; i<row->size-1; ++i){
        row->chars[i] = row->chars[i+1];
    }
    // get rid of final leftover char with null byte
    row->chars[i] = '\0';
    --row->size;
    editor_update_row(row);
}


