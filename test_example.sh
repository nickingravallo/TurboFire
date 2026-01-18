#!/bin/bash
# Quick test example for TurboFire

echo "=== TurboFire Quick Test ==="
echo ""
echo "Test 1: Simple pair ranges (22+)"
echo "Command: ./output/TurboFire \"22+\" \"22+\""
echo ""
./output/TurboFire "22+" "22+"

echo ""
echo "=========================================="
echo ""
echo "Test 2: Wider ranges with flop"
echo "Command: ./output/TurboFire \"22+,A2s+\" \"22+,A2s+\" AcKdQh"
echo ""
./output/TurboFire "22+,A2s+" "22+,A2s+" AcKdQh
