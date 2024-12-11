
#include "editor.h"

// deep copy of a row (for undoing)
stack_entry editor_deep_copy_row(const erow* src, int action) {
    stack_entry copy;

    erow* row = &state.row[state.cy];
    copy.row.idx = row->idx;
    copy.row.hl_open_comment = row->hl_open_comment;
    copy.row.size = row->size;
    copy.row.rsize = row->rsize;

    copy.row.chars = malloc(copy.row.size + 1);
    memcpy(copy.row.chars, row->chars, copy.row.size);
    copy.row.chars[copy.row.size] = '\0';

    copy.row.render = malloc(copy.row.rsize + 1);
    memcpy(copy.row.render, row->render, copy.row.rsize);
    copy.row.render[copy.row.rsize] = '\0';

    copy.row.hl = malloc(copy.row.rsize);
    memcpy(copy.row.hl, row->hl, copy.row.rsize);

    copy.cx = state.cx;
    copy.cy = state.cy;
    copy.action = action;

    return copy;
}

erow editor_copy_row(const erow* src) {
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

void editor_split_row(int row_idx) {
    if (row_idx < 0 || row_idx >= state.num_rows) return;

    erow* row = &state.row[row_idx];
    // split at the end of the original row before merging
    int split_point = row->size - strlen(state.undo.saves[state.undo.stack_size-1].row.chars);

    editor_insert_row(row_idx + 1, row->chars + split_point, row->size - split_point);

    // truncate current row
    row->size = split_point;
    row->chars[split_point] = '\0';

    editor_update_row(row);
}

void editor_merge_row_below(int row_idx) {
    if (row_idx < 0 || row_idx+1 >= state.num_rows) return;

    erow* curr = &state.row[row_idx];
    erow* prev = &state.row[row_idx + 1];

    editor_row_append_string(curr, prev->chars, prev->size);
    editor_delete_row(row_idx+1);
}

// push a row state to undo stack
void editor_push_undo(erow* row, int action) {
    if (state.undo.stack_size >= state.undo.mem_size) {
        state.undo.mem_size = (state.undo.stack_size + 1) * 2;
        state.undo.saves = realloc(state.undo.saves, sizeof(stack_entry) * state.undo.mem_size);
    }

    // push a deep copy of the row onto the stack
    state.undo.saves[state.undo.stack_size] = editor_deep_copy_row(row, action);
    ++state.undo.stack_size;
}

// pop the last state from the undo stack and revert the row
void editor_undo() {
    if (state.undo.stack_size == 0) {
        editor_set_status_msg("Nothing to undo.");
        return;
    }
    stack_entry* entry = &state.undo.saves[state.undo.stack_size - 1];

    state.undoing = 1;
    switch (entry->action) {
    case MODIFY_ROW:
        if (entry->row.idx < state.num_rows) {
            editor_free_row(&state.row[entry->row.idx]);
            state.row[entry->row.idx] = editor_copy_row(&entry->row);
        }
        break;

    case DELETE_ROW:
        editor_insert_row(entry->row.idx, entry->row.chars, entry->row.size);
        break;

    case MERGE_ROW_UP:
        editor_split_row(entry->row.idx);
        break;

    case SPLIT_ROW_DOWN:
        editor_merge_row_below(entry->row.idx);
        break;
    case NEWLINE_ABOVE:
        editor_delete_row(entry->row.idx);
        break;
    }

    // restore cursor position
    state.cx = entry->cx;
    state.cy = entry->cy;

    editor_free_stack_entry(entry);
    --state.undo.stack_size;

    state.dirty = 1;
    editor_set_status_msg("Undo successful.");

    state.undoing = 0;
}

// initialize the undo stack
void editor_init_undo_stack() {
    state.undo.stack_size = 0;
    state.undo.mem_size = 16;
    state.undo.saves = malloc(sizeof(stack_entry) * state.undo.mem_size);
}

// free memory of the undo stack
void editor_free_undo_stack() {
    for (int i = 0; i < state.undo.stack_size; ++i){
        editor_free_stack_entry(&state.undo.saves[i]);
    }
    if(state.undo.saves) free(state.undo.saves);
    state.undo.stack_size = 0;
    state.undo.mem_size = 0;
}

void editor_free_stack_entry(stack_entry* entry) {
    if(entry == NULL) return;
    if(entry->row.chars) free(entry->row.chars);
    if(entry->row.render) free(entry->row.render);
    if(entry->row.hl) free(entry->row.hl);
}
