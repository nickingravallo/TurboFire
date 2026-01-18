#ifndef GUI_H
#define GUI_H

#include "MCCFR.h"
#include "RangeParser.h"

// Strategy data for GUI display
typedef struct {
    char category[16];
    double strategy[3];  // [check/call, bet/raise, fold]
    int board[5];
    int board_size;
    Street street;
} GUIStrategyData;

typedef struct {
    GUIStrategyData *data;
    int count;
    int capacity;
} GUIStrategySet;

// GUI functions
int gui_init(void);
void gui_cleanup(void);
void gui_add_strategy(const char *category, double strategy[3], int *board, int board_size, Street street);
void gui_run(void);
void gui_set_ranges(const char *sb_range_str, const char *bb_range_str);

#endif /* GUI_H */
