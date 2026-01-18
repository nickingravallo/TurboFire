#!/bin/bash
# Test script for SB vs BB range analysis across flop, turn, and river

echo "=== TurboFire Range Test: SB vs BB (Flop, Turn, River) ==="
echo ""

# Test 1: Preflop analysis (will analyze flop, turn, river)
echo "Test 1: Preflop -> Flop/Turn/River Analysis"
echo "SB Range: 22+,A2s+,K2o+"
echo "BB Range: 22+,A2s+"
echo ""
./output/TurboFire "22+,A2s+,K2o+" "22+,A2s+"
echo ""
echo "----------------------------------------"
echo ""

# Test 2: Flop analysis (will analyze turn and river)
echo "Test 2: Flop -> Turn/River Analysis"
echo "SB Range: 22+,A2s+"
echo "BB Range: 22+,A2s+"
echo "Board: AcKdQh"
echo ""
./output/TurboFire "22+,A2s+" "22+,A2s+" AcKdQh
echo ""
echo "----------------------------------------"
echo ""

# Test 3: Turn analysis (will analyze river)
echo "Test 3: Turn -> River Analysis"
echo "SB Range: 22+,A2s+"
echo "BB Range: 22+,A2s+"
echo "Board: AcKdQhJc"
echo ""
./output/TurboFire "22+,A2s+" "22+,A2s+" AcKdQhJc
echo ""
echo "=== Tests Complete ==="
