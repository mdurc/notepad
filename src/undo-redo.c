
#include "editor.h"

// deep copy of a row (for undoing)
erow editor_deep_copy_row(const erow* src) {
    erow copy;
    copy.idx = src->idx;
    copy.hl_open_comment = src->hl_open_comment;
    copy.size = src->size;
    copy.rsize = src->rsize;

    copy.chars = malloc(copy.size + 1);
    memcpy(copy.chars, src->chars, copy.size);
    copy.chars[copy.size] = '\0';

    copy.render = malloc(copy.rsize + 1);
    memcpy(copy.render, src->render, copy.rsize);
    copy.render[copy.rsize] = '\0';

    copy.hl = malloc(copy.rsize);
    memcpy(copy.hl, src->hl, copy.rsize);

    return copy;
}

// push a row state to undo stack
void editor_push_undo(erow* row) {
    if (state.undo.stack_size >= state.undo.mem_size) {
        state.undo.mem_size = (state.undo.stack_size + 1) * 2;
        state.undo.rows = realloc(state.undo.rows, sizeof(erow) * state.undo.mem_size);
    }

    // push a deep copy of the row onto the stack
    state.undo.rows[state.undo.stack_size] = editor_deep_copy_row(row);
    state.undo.stack_size++;
}

// save the current row into undo stack before editing it
void editor_save_row_before_change(erow* row) {
    editor_push_undo(row);
}

// pop the last state from the undo stack and revert the row
void editor_undo() {
    if (state.undo.stack_size == 0) {
        editor_set_status_msg("Nothing to undo.");
        return;
    }

    // get the last saved state
    erow* undo_row = &state.undo.rows[state.undo.stack_size - 1];

    // ensure the undo row index exists in the current rows
    if (undo_row->idx < state.num_rows) {
        editor_free_row(&state.row[undo_row->idx]);
        state.row[undo_row->idx] = editor_deep_copy_row(undo_row);
    } else {
        editor_insert_row(state.num_rows, undo_row->chars, undo_row->size);
    }

    // free the undo row and shrink the stack
    editor_free_row(undo_row);
    state.undo.stack_size--;

    state.dirty = 1;
    editor_set_status_msg("Undo successful.");
}

// initialize the undo stack
void editor_init_undo_stack() {
    state.undo.stack_size = 0;
    state.undo.mem_size = 16;
    state.undo.rows = malloc(sizeof(erow) * state.undo.mem_size);
}

// free memory of the undo stack
void editor_free_undo_stack() {
    for (int i = 0; i < state.undo.stack_size; i++) {
        editor_free_row(&state.undo.rows[i]);
    }
    if(state.undo.rows) free(state.undo.rows);
    state.undo.stack_size = 0;
    state.undo.mem_size = 0;
    state.undo.rows = NULL;
}
