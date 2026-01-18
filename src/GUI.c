/*
 * GUI.c - Graphical User Interface for TurboFire GTO Solver
 * 
 * Displays strategy grids with color-coded actions:
 * - Blue: Check/Call
 * - Green: Bet/Raise  
 * - Red: Fold
 */

#ifdef USE_GUI
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/GUI.h"
#include "../include/MCCFR.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/GUI.h"

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800
#define GRID_SIZE 13
#define CELL_SIZE 40
#define GRID_X_OFFSET 100
#define GRID_Y_OFFSET 150
#define HEADER_HEIGHT 100
#define FOOTER_HEIGHT 50

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static TTF_Font *font = NULL;
static TTF_Font *small_font = NULL;

static GUIStrategySet strategies[3];  // One for each street (flop, turn, river)
static Street current_street = STREET_FLOP;
static int hover_row = -1, hover_col = -1;
static char hover_text[256] = "";
static char sb_range_str[512] = "";
static char bb_range_str[512] = "";
static int board_display[3][5];  // Board cards for each street
static int board_size[3];       // Board size for each street

// Color definitions
static SDL_Color color_bg = {20, 20, 30, 255};
static SDL_Color color_grid = {60, 60, 80, 255};
static SDL_Color color_text = {255, 255, 255, 255};
static SDL_Color color_check = {100, 150, 255, 255};  // Blue
static SDL_Color color_bet = {100, 255, 100, 255};    // Green
static SDL_Color color_fold = {255, 100, 100, 255};   // Red
static SDL_Color color_hover = {255, 255, 200, 255};

static const char *RANKS = "23456789TJQKA";

int gui_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 0;
    }
    
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF initialization failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 0;
    }
    
    window = SDL_CreateWindow("TurboFire GTO Solver",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH,
                              WINDOW_HEIGHT,
                              SDL_WINDOW_SHOWN);
    
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 0;
    }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 0;
    }
    
    // Try to load fonts (fallback to default if not found)
    font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16);
    if (!font) {
        font = TTF_OpenFont("/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", 16);
    }
    if (!font) {
        font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 16);
    }
    
    small_font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12);
    if (!small_font) {
        small_font = TTF_OpenFont("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 12);
    }
    if (!small_font) {
        small_font = font;  // Fallback to main font
    }
    
    // Initialize strategy sets
    for (int i = 0; i < 3; i++) {
        strategies[i].capacity = 200;
        strategies[i].count = 0;
        strategies[i].data = calloc(strategies[i].capacity, sizeof(GUIStrategyData));
        board_size[i] = 0;
        for (int j = 0; j < 5; j++) {
            board_display[i][j] = -1;
        }
    }
    
    return 1;
#else
    // Stub implementation when GUI is disabled
    return 0;
#endif
}

void gui_cleanup(void) {
#ifdef USE_GUI
    for (int i = 0; i < 3; i++) {
        if (strategies[i].data) {
            free(strategies[i].data);
        }
    }
    
    if (font) TTF_CloseFont(font);
    if (small_font && small_font != font) TTF_CloseFont(small_font);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
#endif
}

void gui_set_ranges(const char *sb_range_str_in, const char *bb_range_str_in) {
#ifdef USE_GUI
    strncpy(sb_range_str, sb_range_str_in ? sb_range_str_in : "", sizeof(sb_range_str) - 1);
    strncpy(bb_range_str, bb_range_str_in ? bb_range_str_in : "", sizeof(bb_range_str) - 1);
#else
    // Stub - no-op when GUI disabled
    (void)sb_range_str_in;
    (void)bb_range_str_in;
#endif
}

void gui_add_strategy(const char *category, double strategy[3], int *board, int board_size_in, Street street) {
#ifdef USE_GUI
    if (street < 0 || street > 2) return;
    
    GUIStrategySet *set = &strategies[street];
    if (set->count >= set->capacity) {
        set->capacity *= 2;
        set->data = realloc(set->data, set->capacity * sizeof(GUIStrategyData));
    }
    
    GUIStrategyData *data = &set->data[set->count++];
    strncpy(data->category, category, 15);
    data->category[15] = '\0';
    memcpy(data->strategy, strategy, sizeof(double) * 3);
    data->street = street;
    data->board_size = board_size_in;
    for (int i = 0; i < 5; i++) {
        data->board[i] = (i < board_size_in) ? board[i] : -1;
    }
    
    // Update board display for this street
    if (board_size_in > 0) {
        board_size[street] = board_size_in;
        for (int i = 0; i < board_size_in && i < 5; i++) {
            board_display[street][i] = board[i];
        }
    }
#else
    // Stub - no-op when GUI disabled
    (void)category;
    (void)strategy;
    (void)board;
    (void)board_size_in;
    (void)street;
#endif
}

static void card_str(int card, char *out) {
    if (card < 0 || card >= 52) {
        out[0] = '\0';
        return;
    }
    out[0] = RANKS[card >> 2];
    const char *suits = "cdhs";
    out[1] = suits[card & 3];
    out[2] = '\0';
}

#ifdef USE_GUI
static void render_text(const char *text, int x, int y, TTF_Font *f, SDL_Color color) {
    if (!f) return;
    
    SDL_Surface *surface = TTF_RenderText_Solid(f, text, color);
    if (!surface) return;
    
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect rect = {x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, NULL, &rect);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}
#endif

static void get_hand_coords(const char *category, int *row, int *col, int *is_suited, int *is_pair) {
    *row = *col = -1;
    *is_suited = *is_pair = 0;
    
    int len = strlen(category);
    if (len < 2) return;
    
    int r1 = -1, r2 = -1;
    for (int i = 0; i < 13; i++) {
        if (RANKS[i] == category[0]) r1 = i;
        if (RANKS[i] == category[1]) r2 = i;
    }
    
    if (r1 < 0 || r2 < 0) return;
    
    if (r1 == r2) {
        *is_pair = 1;
        *row = *col = r1;
    } else {
        int high = (r1 > r2) ? r1 : r2;
        int low = (r1 < r2) ? r1 : r2;
        
        if (len > 2 && category[2] == 's') {
            *is_suited = 1;
            *row = low;
            *col = high;
        } else {
            *row = high;
            *col = low;
        }
    }
}

#ifdef USE_GUI
static void render_grid(void) {
    GUIStrategySet *set = &strategies[current_street];
    
    // Draw grid background
    SDL_Rect grid_rect = {GRID_X_OFFSET - 5, GRID_Y_OFFSET - 5,
                          GRID_SIZE * CELL_SIZE + 10, GRID_SIZE * CELL_SIZE + 10};
    SDL_SetRenderDrawColor(renderer, color_grid.r, color_grid.g, color_grid.b, 255);
    SDL_RenderFillRect(renderer, &grid_rect);
    
    // Draw rank labels
    for (int i = 0; i < 13; i++) {
        char label[2] = {RANKS[i], '\0'};
        render_text(label, GRID_X_OFFSET - 20, GRID_Y_OFFSET + i * CELL_SIZE + CELL_SIZE/2 - 8, font, color_text);
        render_text(label, GRID_X_OFFSET + i * CELL_SIZE + CELL_SIZE/2 - 4, GRID_Y_OFFSET - 25, font, color_text);
    }
    
    // Draw "s" and "o" labels
    render_text("s", GRID_X_OFFSET - 40, GRID_Y_OFFSET + 2, small_font, color_text);
    render_text("o", GRID_X_OFFSET - 40, GRID_Y_OFFSET + GRID_SIZE * CELL_SIZE - 12, small_font, color_text);
    
    // Draw cells
    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            int x = GRID_X_OFFSET + col * CELL_SIZE;
            int y = GRID_Y_OFFSET + row * CELL_SIZE;
            
            // Determine cell type
            int is_pair = (row == col);
            int is_suited = (row < col);
            int is_offsuit = (row > col);
            
            // Find matching strategy data
            double cell_strategy[3] = {0.33, 0.33, 0.34};
            int has_data = 0;
            char cell_category[16] = "";
            
            for (int i = 0; i < set->count; i++) {
                int s_row, s_col, s_suited, s_pair;
                get_hand_coords(set->data[i].category, &s_row, &s_col, &s_suited, &s_pair);
                
                if (s_row == row && s_col == col) {
                    if ((is_pair && s_pair) || (is_suited && s_suited) || (is_offsuit && !s_suited && !s_pair)) {
                        memcpy(cell_strategy, set->data[i].strategy, sizeof(cell_strategy));
                        strncpy(cell_category, set->data[i].category, 15);
                        has_data = 1;
                        break;
                    }
                }
            }
            
            // Calculate color based on strategy
            SDL_Color cell_color;
            if (has_data) {
                int r = (int)(color_check.r * cell_strategy[0] + color_bet.r * cell_strategy[1] + color_fold.r * cell_strategy[2]);
                int g = (int)(color_check.g * cell_strategy[0] + color_bet.g * cell_strategy[1] + color_fold.g * cell_strategy[2]);
                int b = (int)(color_check.b * cell_strategy[0] + color_bet.b * cell_strategy[1] + color_fold.b * cell_strategy[2]);
                cell_color = (SDL_Color){r, g, b, 255};
            } else {
                cell_color = (SDL_Color){40, 40, 50, 255};
            }
            
            // Highlight hover
            if (hover_row == row && hover_col == col) {
                cell_color.r = (cell_color.r + color_hover.r) / 2;
                cell_color.g = (cell_color.g + color_hover.g) / 2;
                cell_color.b = (cell_color.b + color_hover.b) / 2;
            }
            
            SDL_Rect cell_rect = {x, y, CELL_SIZE - 1, CELL_SIZE - 1};
            SDL_SetRenderDrawColor(renderer, cell_color.r, cell_color.g, cell_color.b, 255);
            SDL_RenderFillRect(renderer, &cell_rect);
        }
    }
}

static void render_header(void) {
#ifdef USE_GUI
    // Title
    render_text("TurboFire GTO Solver", 20, 10, font, color_text);
    
    // Street buttons
    const char *street_names[] = {"Flop", "Turn", "River"};
    for (int i = 0; i < 3; i++) {
        int x = 20 + i * 100;
        int y = 40;
        SDL_Color btn_color = (current_street == i) ? color_bet : color_grid;
        SDL_Rect btn_rect = {x, y, 80, 30};
        SDL_SetRenderDrawColor(renderer, btn_color.r, btn_color.g, btn_color.b, 255);
        SDL_RenderFillRect(renderer, &btn_rect);
        render_text(street_names[i], x + 20, y + 5, small_font, color_text);
    }
    
    // Board display
    char board_text[64] = "Board: ";
    if (board_size[current_street] > 0) {
        for (int i = 0; i < board_size[current_street]; i++) {
            char card[3];
            card_str(board_display[current_street][i], card);
            strcat(board_text, card);
            strcat(board_text, " ");
        }
    } else {
        strcat(board_text, "Random");
    }
    render_text(board_text, 350, 45, small_font, color_text);
    
    // Legend
    render_text("Check/Call", WINDOW_WIDTH - 200, 20, small_font, color_check);
    render_text("Bet/Raise", WINDOW_WIDTH - 200, 40, small_font, color_bet);
    render_text("Fold", WINDOW_WIDTH - 200, 60, small_font, color_fold);
#endif
}

static void render_hover_tooltip(void) {
#ifdef USE_GUI
    if (hover_row < 0 || hover_col < 0 || strlen(hover_text) == 0) return;
    
    int x = GRID_X_OFFSET + hover_col * CELL_SIZE + CELL_SIZE;
    int y = GRID_Y_OFFSET + hover_row * CELL_SIZE;
    
    if (x + 200 > WINDOW_WIDTH) x = GRID_X_OFFSET + hover_col * CELL_SIZE - 200;
    if (y + 80 > WINDOW_HEIGHT) y = GRID_Y_OFFSET + hover_row * CELL_SIZE - 80;
    
    SDL_Rect tooltip_rect = {x, y, 200, 80};
    SDL_SetRenderDrawColor(renderer, 40, 40, 50, 240);
    SDL_RenderFillRect(renderer, &tooltip_rect);
    
    SDL_SetRenderDrawColor(renderer, color_text.r, color_text.g, color_text.b, 255);
    SDL_RenderDrawRect(renderer, &tooltip_rect);
    
    render_text(hover_text, x + 10, y + 10, small_font, color_text);
}
#endif

void gui_run(void) {
#ifdef USE_GUI
    if (!window || !renderer) return;
    
    SDL_Event e;
    int running = 1;
    
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int x = e.button.x;
                int y = e.button.y;
                
                // Check street buttons
                if (y >= 40 && y <= 70) {
                    for (int i = 0; i < 3; i++) {
                        if (x >= 20 + i * 100 && x <= 100 + i * 100) {
                            current_street = i;
                            hover_row = hover_col = -1;
                            hover_text[0] = '\0';
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                int x = e.motion.x;
                int y = e.motion.y;
                
                // Check if mouse is over grid
                if (x >= GRID_X_OFFSET && x < GRID_X_OFFSET + GRID_SIZE * CELL_SIZE &&
                    y >= GRID_Y_OFFSET && y < GRID_Y_OFFSET + GRID_SIZE * CELL_SIZE) {
                    hover_col = (x - GRID_X_OFFSET) / CELL_SIZE;
                    hover_row = (y - GRID_Y_OFFSET) / CELL_SIZE;
                    
                    // Build hover text
                    GUIStrategySet *set = &strategies[current_street];
                    hover_text[0] = '\0';
                    
                    for (int i = 0; i < set->count; i++) {
                        int s_row, s_col, s_suited, s_pair;
                        get_hand_coords(set->data[i].category, &s_row, &s_col, &s_suited, &s_pair);
                        
                        int is_pair = (hover_row == hover_col);
                        int is_suited = (hover_row < hover_col);
                        
                        if (s_row == hover_row && s_col == hover_col) {
                            if ((is_pair && s_pair) || (is_suited && s_suited) || (!is_suited && !s_pair)) {
                                snprintf(hover_text, sizeof(hover_text),
                                        "%s\nCheck: %.1f%%\nBet: %.1f%%\nFold: %.1f%%",
                                        set->data[i].category,
                                        set->data[i].strategy[0] * 100,
                                        set->data[i].strategy[1] * 100,
                                        set->data[i].strategy[2] * 100);
                                break;
                            }
                        }
                    }
                    
                    if (hover_text[0] == '\0') {
                        char hand_label[8];
                        if (hover_row == hover_col) {
                            snprintf(hand_label, sizeof(hand_label), "%c%c", RANKS[hover_row], RANKS[hover_row]);
                        } else if (hover_row < hover_col) {
                            snprintf(hand_label, sizeof(hand_label), "%c%cs", RANKS[hover_col], RANKS[hover_row]);
                        } else {
                            snprintf(hand_label, sizeof(hand_label), "%c%co", RANKS[hover_row], RANKS[hover_col]);
                        }
                        snprintf(hover_text, sizeof(hover_text), "%s\nNo data", hand_label);
                    }
                } else {
                    hover_row = hover_col = -1;
                    hover_text[0] = '\0';
                }
            }
        }
        
        // Render
        SDL_SetRenderDrawColor(renderer, color_bg.r, color_bg.g, color_bg.b, 255);
        SDL_RenderClear(renderer);
        
        render_header();
        render_grid();
        render_hover_tooltip();
        
        SDL_RenderPresent(renderer);
        SDL_Delay(16);  // ~60 FPS
    }
#else
    // Stub - no-op when GUI disabled
#endif
}
