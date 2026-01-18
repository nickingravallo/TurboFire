#!/bin/bash
# Example command using the converted ranges from JSON format

# Button RFI Range (converted from JSON)
BTN_RFI="77@50%,66@50%,44,33,22,A8s,A7s@50%,A6s,A4s@50%,A3s@50%,A2s,A9o,A8o,A7o,A6o,A5o@50%,A4o@50%,A3o@50%,A2o,K9s@50%,K8s,K7s,K6s,K5s,K4s,K3s,K2s,KJo@50%,KTo@50%,K9o,K8o,K7o@50%,K6o,K5o,QTs@50%,Q9s,Q8s@50%,Q7s,Q6s,Q5s,Q4s,Q3s,Q2s,QTo@50%,QJo@50%,Q9o@50%,Q8o,Q7o,JTs@50%,J9s@50%,J8s,J7s,J6s,J5s,J4s,J3s,J2s,J9o,JTo,J8o,T9s@50%,T8s,T7s@50%,T6s,T5s,T4s,T3s,T2s,T8o@50%,T9o@50%,T7o@50%,95s@50%,94s,93s,92s,98o@50%,97o@50%,84s@50%,87o,74s,73s,64s@50%,63s@50%,65o,54s@50%,53s,52s,43s@50%,42s"

# BB Call Range (converted from JSON)
BB_CALL="77@50%,66@50%,44,33,22,A8s,A7s@50%,A6s,A4s@50%,A3s@50%,A2s,A9o,A8o,A7o,A6o,A5o@50%,A4o@50%,A3o@50%,A2o,K9s@50%,K8s,K7s,K6s,K5s,K4s,K3s,K2s,KJo@50%,KTo@50%,K9o,K8o,K7o@50%,K6o,K5o,QTs@50%,Q9s,Q8s@50%,Q7s,Q6s,Q5s,Q4s,Q3s,Q2s,QTo@50%,QJo@50%,Q9o@50%,Q8o,Q7o,JTs@50%,J9s@50%,J8s,J7s,J6s,J5s,J4s,J3s,J2s,J9o,JTo,J8o,T9s@50%,T8s,T7s@50%,T6s,T5s,T4s,T3s,T2s,T8o@50%,T9o@50%,T7o@50%,95s@50%,94s,93s,92s,98o@50%,97o@50%,84s@50%,87o,74s,73s,64s@50%,63s@50%,65o,54s@50%,53s,52s,43s@50%,42s"

# Run TurboFire with these ranges
./output/TurboFire "$BTN_RFI" "$BB_CALL"

# Or with a specific board:
# ./output/TurboFire "$BTN_RFI" "$BB_CALL" "KcQc2d"
