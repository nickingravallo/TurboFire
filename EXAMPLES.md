# TurboFire GTO Solver - Example Commands

## Quick Test Examples

### Example 1: Tight Ranges (Preflop → Flop/Turn/River)
```bash
./output/TurboFire "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o" "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o"
```

### Example 2: SB vs BB Wide Ranges
```bash
./output/TurboFire "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o" "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o"
```

### Example 3: Simple Test (Pairs Only)
```bash
./output/TurboFire "22+" "22+"
```

### Example 4: With Flop Board
```bash
./output/TurboFire "22+,A2s+" "22+,A2s+" AcKdQh
```

### Example 5: With Turn Board
```bash
./output/TurboFire "22+,A2s+" "22+,A2s+" AcKdQhJc
```

## Common Range Examples

### Tight Range (Top 20%)
```
22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o
```

### Medium Range (Top 40%)
```
22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o
```

### Wide Range (Top 60%)
```
22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o
```

## Quick Copy-Paste Examples

### Test 1: Simple Pair Range
```bash
./output/TurboFire "22+,33+,44+" "22+,33+,44+"
```

### Test 2: Premium Hands Only
```bash
./output/TurboFire "AA,KK,QQ,JJ,TT,99,88,AKs,AQs,AJs,ATs,KQs,KJs,KTs,QJs,QTs,JTs,AKo,AQo,AJo,ATo,KQo,KJo,KTo,QJo,QTo,JTo" "AA,KK,QQ,JJ,TT,99,88,AKs,AQs,AJs,ATs,KQs,KJs,KTs,QJs,QTs,JTs,AKo,AQo,AJo,ATo,KQo,KJo,KTo,QJo,QTo,JTo"
```

### Test 3: With Specific Flop
```bash
./output/TurboFire "22+,A2s+" "22+,A2s+" AcKdQh
```

### Test 4: With Specific Turn
```bash
./output/TurboFire "22+,A2s+" "22+,A2s+" AcKdQhJc
```
