def get_payoff(p1_strat, p2_strat):
    #calculate utlity


alpha = 1.5
beta = 0
gamma = 2
def update_regrets(node, actions, infoset):
    for action in actions:
        r = 

def get_strategy_for_node(actions, regrets, infoset):
    normalized_sum = 0;
    strategy = [0.0] * len(actions)
    for (i in range len(actions)):
        if (regret[i] > 0):
            normalized_sum += regrets[i]
            strategy[i] = regrets[i]
        else:
            strategy[i] = 0;

    if normalized_sum > 0:
        for (i in range len(strategy)):
            strategy[i] = strategy[i] / normalized_sum
    else 
        for (i in range len(strategy)):
            strategy[i] = 1.0 / len(actions)

    return strategy;

def reach_prob(actions, infoset, player, hands_range):
    strategy = get_strategy_for_node(action, infoset, player);
    for action in actions:
        for hand in hands_range: #poker range
            idx = (action * num_hands) + hand
            if player == 0:
                next_p1_reach[hand] *= strategy[idx]
            else:
                next_p2_reach[hand] *= strategy[idx]

def update_regret(actions, infoset, player, hands_range, p1_reach, p2_reach, out_util, action_util):
    for action in actions:
        for hand in hands_range:
            idx = (action * len(hands_range)) + hand
            if (player == 0):
                opp_reach = p2_reach[hand]
                my_reach = p1_reach[hand]
            else:
                opp_reach = p1_reach[hand]
                my_reach = p2_reach[hand]
            
            #regret = ev of taking action a at history h - average ev of the node given the strategy
            regret = action_utils[idx] - out_util[idx]
            infoset.regret_sum[idx] = regret * opp_reach



