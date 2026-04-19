#include "parse.h"
#include "ex.h"

int main() {
    // 2. Parse it
    PlayerRange p1_range;
    parse_json_range(ex_btn_hu_json_data, &p1_range);

    printf("Successfully generated %d specific 64-bit hand combinations.\n", p1_range.num_combos);

    print_range_grid(&p1_range);
    return 0;
}
